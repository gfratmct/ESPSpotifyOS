#include "net/spotify_api.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <esp_log.h>
#include <cJSON.h>

#include "data/auth_state.h"
#include "net/http_client.h"
#include "net/spotify_auth.h"

#define TAG "spotify_api"

#define SPOTIFY_API_BASE "https://api.spotify.com/v1"

// http_response_t is ~8.7 KB — always heap-allocate it, never put it on the stack
static http_response_t *resp_alloc(void)
{
    return calloc(1, sizeof(http_response_t));
}

// GET with Bearer auth; transparently refreshes and retries once on 401.
// Caller must free(*resp_out) on ESP_OK.
static esp_err_t spotify_api_get(const char *url, http_response_t **resp_out)
{
    auth_state_t *state = auth_state_get();
    if (!state || !state->is_logged_in) {
        return ESP_ERR_INVALID_STATE;
    }

    // proactive refresh if the token looks expired (requires SNTP-synced time)
    if (state->token_expires_at > 0 && (uint32_t)time(NULL) >= state->token_expires_at) {
        ESP_LOGI(TAG, "Access token expired, refreshing");
        esp_err_t rerr = spotify_refresh_access_token();
        if (rerr != ESP_OK) return rerr;
    }

    http_response_t *resp = resp_alloc();
    if (!resp) return ESP_ERR_NO_MEM;

    char bearer[576]; // "Bearer " + 512-byte token + NUL, with headroom
    for (int attempt = 0; attempt < 2; attempt++) {
        snprintf(bearer, sizeof(bearer), "Bearer %s", (char *)state->spotify_token);
        http_header_t headers[] = {
            { .key = "Authorization", .value = bearer },
        };

        esp_err_t err = http_get(url, headers, 1, resp);

        if (err != ESP_OK && resp->status_code != 401) {
            ESP_LOGE(TAG, "GET %s failed: %s", url, esp_err_to_name(err));
            free(resp);
            return err;
        }

        if (resp->status_code == 401 && attempt == 0) {
            ESP_LOGW(TAG, "401 from Spotify, refreshing token and retrying");

            if (spotify_refresh_access_token() != ESP_OK) {
                free(resp);
                return ESP_FAIL;
            }

            state = auth_state_get();
            continue;
        }

        if (resp->status_code != 200) {
            ESP_LOGE(TAG, "GET %s -> %d: %.200s", url, resp->status_code, resp->data);
            free(resp);
            return ESP_FAIL;
        }

        *resp_out = resp;
        return ESP_OK;
    }

    free(resp);
    return ESP_FAIL;
}

esp_err_t spotify_get_auth_info(spotify_auth_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    http_response_t *resp = NULL;
    esp_err_t err = spotify_api_get(SPOTIFY_API_BASE "/me", &resp);
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(resp->data);
    free(resp);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse /me response");
        return ESP_FAIL;
    }

    const cJSON *id      = cJSON_GetObjectItem(root, "id");
    const cJSON *name    = cJSON_GetObjectItem(root, "display_name");
    const cJSON *product = cJSON_GetObjectItem(root, "product");

    if (cJSON_IsString(id))      strncpy(out->account_id, id->valuestring, sizeof(out->account_id) - 1);
    if (cJSON_IsString(name) && name->valuestring)
        strncpy(out->display_name, name->valuestring, sizeof(out->display_name) - 1);
    if (cJSON_IsString(product)) strncpy(out->product, product->valuestring, sizeof(out->product) - 1);
    cJSON_Delete(root);

    out->is_logged_in = (out->account_id[0] != '\0');
    ESP_LOGI(TAG, "Logged in as '%s' (%s, %s)", out->display_name, out->account_id, out->product);
    return ESP_OK;
}

esp_err_t spotify_get_saved_tracks(int limit, int offset, const char *market,
                                   spotify_track_t *out_tracks, size_t max_tracks,
                                   size_t *out_count, int *out_total)
{
    if (!out_tracks || !out_count || max_tracks == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (limit < 1) limit = 1;
    if (limit > 50) limit = 50;
    *out_count = 0;

    // Request only the fields we parse: a full /me/tracks item is ~3-4 KB
    // (album, markets, external URLs), which overflows MAX_BODY_SIZE quickly.
    #define SPOTIFY_TRACK_FIELDS "total,items(track(artists(name),duration_ms,id,name,uri))"

    char url[256];
    if (market) {
        snprintf(url, sizeof(url),
                 SPOTIFY_API_BASE "/me/tracks?limit=%d&offset=%d&market=%s&fields=" SPOTIFY_TRACK_FIELDS,
                 limit, offset, market);
    } else {
        snprintf(url, sizeof(url),
                 SPOTIFY_API_BASE "/me/tracks?limit=%d&offset=%d&fields=" SPOTIFY_TRACK_FIELDS,
                 limit, offset);
    }

    http_response_t *resp = NULL;
    esp_err_t err = spotify_api_get(url, &resp);
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(resp->data);
    bool was_truncated = resp->truncated;
    free(resp);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse tracks response (%s)",
                 was_truncated ? "body truncated — reduce limit/fields" : "malformed JSON");
        return ESP_FAIL;
    }

    const cJSON *total = cJSON_GetObjectItem(root, "total");
    if (out_total && cJSON_IsNumber(total)) {
        *out_total = total->valueint;
    }

    const cJSON *items = cJSON_GetObjectItem(root, "items");
    if (!cJSON_IsArray(items)) {
        ESP_LOGE(TAG, "No items array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    const cJSON *item;
    cJSON_ArrayForEach(item, items) {
        if (*out_count >= max_tracks) break;

        const cJSON *track = cJSON_GetObjectItem(item, "track");
        if (!cJSON_IsObject(track)) continue;

        spotify_track_t *t = &out_tracks[*out_count];
        memset(t, 0, sizeof(*t));

        const cJSON *id   = cJSON_GetObjectItem(track, "id");
        const cJSON *name = cJSON_GetObjectItem(track, "name");
        const cJSON *uri  = cJSON_GetObjectItem(track, "uri");
        const cJSON *dur  = cJSON_GetObjectItem(track, "duration_ms");

        if (cJSON_IsString(id))   strncpy(t->track_id, id->valuestring, sizeof(t->track_id) - 1);
        if (cJSON_IsString(name)) strncpy(t->track_name, name->valuestring, sizeof(t->track_name) - 1);
        if (cJSON_IsString(uri))  strncpy(t->uri, uri->valuestring, sizeof(t->uri) - 1);
        if (cJSON_IsNumber(dur))  t->duration_ms = dur->valueint;

        // first artist only
        const cJSON *artists = cJSON_GetObjectItem(track, "artists");
        if (cJSON_IsArray(artists)) {
            const cJSON *artist = cJSON_GetArrayItem(artists, 0);
            const cJSON *aname  = artist ? cJSON_GetObjectItem(artist, "name") : NULL;
            if (cJSON_IsString(aname)) {
                strncpy(t->track_artist, aname->valuestring, sizeof(t->track_artist) - 1);
            }
        }
        (*out_count)++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Parsed %u saved tracks (offset=%d)", (unsigned)*out_count, offset);
    return ESP_OK;
}

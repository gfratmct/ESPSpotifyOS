#include "spotify.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <esp_log.h>
#include <mbedtls/base64.h>
#include <cJSON.h>

#include "utils/http.h"
#include "data/state.h"

#define TAG "spotify"

#define SPOTIFY_AUTHORIZE_URL "https://accounts.spotify.com/authorize"
#define SPOTIFY_TOKEN_URL     "https://accounts.spotify.com/api/token"
#define SPOTIFY_API_BASE      "https://api.spotify.com/v1"

#define SPOTIFY_CLIENT_ID     CONFIG_PLAYER_SPOTIFY_CLIENT_ID
#define SPOTIFY_CLIENT_SECRET CONFIG_PLAYER_SPOTIFY_CLIENT_SECRET
#define SPOTIFY_REDIRECT_URI  "http://127.0.0.1:8080/callback"
#define SPOTIFY_SCOPES \
    "user-read-private user-read-email user-library-read user-read-recently-played " \
    "playlist-read-private user-read-playback-state user-modify-playback-state " \
    "user-read-currently-playing app-remote-control streaming"

// ---- internal helpers -------------------------------------------------------

// http_response_t is ~8.7 KB — always heap-allocate it, never put it on the stack
static http_response_t *resp_alloc(void)
{
    return calloc(1, sizeof(http_response_t));
}

static esp_err_t build_basic_auth_header(char *out, size_t out_size)
{
    char credentials[96];
    uint8_t b64[128];
    size_t b64_len = 0;

    int cred_len = snprintf(credentials, sizeof(credentials), "%s:%s",
                            SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET);
    if (cred_len <= 0 || cred_len >= (int)sizeof(credentials)) {
        return ESP_ERR_INVALID_SIZE;
    }
    // separate input/output buffers — mbedtls_base64_encode cannot work in-place
    int ret = mbedtls_base64_encode(b64, sizeof(b64), &b64_len,
                                    (const uint8_t *)credentials, cred_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "base64 encode failed: -0x%04x", -ret);
        return ESP_FAIL;
    }
    b64[b64_len] = '\0';
    snprintf(out, out_size, "Basic %s", b64);
    return ESP_OK;
}

static esp_err_t store_tokens(const char *access_token, const char *refresh_token, int expires_in)
{
    app_state_t *state = get_app_state();
    if (!state || !access_token) {
        return ESP_ERR_INVALID_STATE;
    }
    strncpy((char *)state->spotify_token, access_token, sizeof(state->spotify_token) - 1);
    state->spotify_token[sizeof(state->spotify_token) - 1] = '\0';

    if (refresh_token) { // Spotify only sometimes rotates the refresh token
        strncpy((char *)state->refresh_token, refresh_token, sizeof(state->refresh_token) - 1);
        state->refresh_token[sizeof(state->refresh_token) - 1] = '\0';
    }

    if (expires_in > 0) {
        state->token_expires_at = (uint32_t)time(NULL) + (uint32_t)expires_in - 60; // 60s safety margin
    }
    state->is_logged_in = true;
    return save_app_state();
}

// Parses {"access_token", "refresh_token"?, "expires_in"} from a token endpoint
// response and stores it into app_state.
static esp_err_t handle_token_response(http_response_t *resp)
{
    cJSON *root = cJSON_Parse(resp->data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse token response: %.200s", resp->data);
        return ESP_FAIL;
    }

    const cJSON *at = cJSON_GetObjectItem(root, "access_token");
    const cJSON *rt = cJSON_GetObjectItem(root, "refresh_token");
    const cJSON *ei = cJSON_GetObjectItem(root, "expires_in");

    esp_err_t result = ESP_FAIL;
    if (cJSON_IsString(at)) {
        result = store_tokens(at->valuestring,
                              cJSON_IsString(rt) ? rt->valuestring : NULL,
                              cJSON_IsNumber(ei) ? ei->valueint : 0);
    } else {
        ESP_LOGE(TAG, "Unexpected token response shape: %.200s", resp->data);
    }
    cJSON_Delete(root);
    return result;
}

// ---- public: authorization / authentication ---------------------------------

void spotify_logout(void)
{
    app_state_t *state = get_app_state();
    if (!state || !state->is_logged_in) {
        return; // no session to drop — no state change
    }
    ESP_LOGW(TAG, "Spotify session invalid — logging out, back to login screen");
    memset(state->spotify_token, 0, sizeof(state->spotify_token));
    memset(state->refresh_token, 0, sizeof(state->refresh_token));
    state->token_expires_at = 0;
    state->is_logged_in = false;
    state->current_screen = SCREEN_ID_SETUP; // the ui_state loop switches back
    save_app_state();
}

esp_err_t spotify_get_authorize_url(char *out, size_t out_size)
{
    if (!out || out_size == 0) return ESP_ERR_INVALID_ARG;
    int written = snprintf(out, out_size,
                          "%s?client_id=%s&response_type=code&redirect_uri=%s&scope=%s",
                          SPOTIFY_AUTHORIZE_URL, SPOTIFY_CLIENT_ID,
                          SPOTIFY_REDIRECT_URI, SPOTIFY_SCOPES);
    if (written < 0 || written >= (int)out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t spotify_exchange_code_for_token(const char *code)
{
    if (!code || !*code) return ESP_ERR_INVALID_ARG;

    char auth_header[192];
    esp_err_t err = build_basic_auth_header(auth_header, sizeof(auth_header));
    if (err != ESP_OK) return err;

    char body[768];
    int written = snprintf(body, sizeof(body),
                           "grant_type=authorization_code&code=%s&redirect_uri=%s",
                           code, SPOTIFY_REDIRECT_URI);
    if (written < 0 || written >= (int)sizeof(body)) {
        ESP_LOGE(TAG, "Authorization code too long for body buffer");
        return ESP_ERR_INVALID_SIZE;
    }

    http_header_t headers[] = {
        { .key = "Authorization", .value = auth_header },
    };

    http_response_t *resp = resp_alloc();
    if (!resp) return ESP_ERR_NO_MEM;

    err = http_post(SPOTIFY_TOKEN_URL, body, "application/x-www-form-urlencoded", headers, 1, resp);
    if (err != ESP_OK || resp->status_code != 200) {
        ESP_LOGE(TAG, "Token exchange failed (err=%s, status=%d): %.200s",
                 esp_err_to_name(err), resp->status_code, resp->data);
        free(resp);
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    err = handle_token_response(resp);
    free(resp);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Spotify authorization complete");
    }
    return err;
}

esp_err_t spotify_refresh_access_token(void)
{
    app_state_t *state = get_app_state();
    if (!state || state->refresh_token[0] == '\0') {
        ESP_LOGE(TAG, "No refresh token available");
        spotify_logout();
        return ESP_ERR_INVALID_STATE;
    }

    char auth_header[192];
    esp_err_t err = build_basic_auth_header(auth_header, sizeof(auth_header));
    if (err != ESP_OK) {
        spotify_logout();
        return err;
    }

    char body[640]; // "grant_type=refresh_token&refresh_token=" + 511-char token + NUL
    snprintf(body, sizeof(body), "grant_type=refresh_token&refresh_token=%s",
             (char *)state->refresh_token);

    http_header_t headers[] = {
        { .key = "Authorization", .value = auth_header },
    };

    http_response_t *resp = resp_alloc();
    if (!resp) return ESP_ERR_NO_MEM;

    err = http_post(SPOTIFY_TOKEN_URL, body, "application/x-www-form-urlencoded", headers, 1, resp);
    if (err != ESP_OK && resp->status_code == 0) {
        ESP_LOGE(TAG, "Token refresh transport error (%s)", esp_err_to_name(err));
        free(resp);
        spotify_logout();
        return err;
    }
    if (resp->status_code == 400 || resp->status_code == 401 || resp->status_code == 403) {
        // Spotify rejected the refresh token (e.g. invalid_grant) — dead session
        ESP_LOGE(TAG, "Token refresh rejected (status=%d): %.200s",
                 resp->status_code, resp->data);
        free(resp);
        spotify_logout();
        return ESP_FAIL;
    }
    if (resp->status_code != 200) {
        ESP_LOGE(TAG, "Token refresh failed (status=%d): %.200s",
                 resp->status_code, resp->data);
        free(resp);
        spotify_logout();
        return ESP_FAIL;
    }

    err = handle_token_response(resp);
    free(resp);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Access token refreshed");
    } else {
        spotify_logout();
    }
    return err;
}

// GET with Bearer auth; transparently refreshes and retries once on 401.
// Caller must free(*resp_out) on ESP_OK.
static esp_err_t spotify_api_get(const char *url, http_response_t **resp_out)
{
    app_state_t *state = get_app_state();
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

            state = get_app_state();
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

// ---- public: profile / library -----------------------------------------------

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
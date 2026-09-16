#include "media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include <cJSON.h>

#include "utils/http.h"

#define TAG "media"

#define MEDIA_BASE_URL CONFIG_PLAYER_MEDIA_SERVICE_URL
#define MEDIA_API_KEY  CONFIG_PLAYER_MEDIA_API_KEY

#define MEDIA_IMPORT_TIMEOUT_MS 120000 // yt-dlp download can take a while
#define MEDIA_LIST_TIMEOUT_MS   10000

// http_response_t is ~8.7 KB — always heap-allocate it, never put it on the stack
static http_response_t *media_resp_alloc(void)
{
    return calloc(1, sizeof(http_response_t));
}

static bool media_configured(void)
{
    return MEDIA_BASE_URL[0] != '\0' && MEDIA_API_KEY[0] != '\0';
}

// Parses a single media track object into out (all fields optional).
static void media_parse_track(const cJSON *obj, media_track_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    const cJSON *id   = cJSON_GetObjectItem(obj, "id");
    const cJSON *name = cJSON_GetObjectItem(obj, "title");
    const cJSON *art  = cJSON_GetObjectItem(obj, "artist");
    const cJSON *dur  = cJSON_GetObjectItem(obj, "duration");
    const cJSON *enc  = cJSON_GetObjectItem(obj, "encoding");

    if (cJSON_IsNumber(id)) out->id = id->valueint;
    if (cJSON_IsString(name)) strncpy(out->title, name->valuestring, sizeof(out->title) - 1);
    if (cJSON_IsString(art)) strncpy(out->artist, art->valuestring, sizeof(out->artist) - 1);
    if (cJSON_IsNumber(dur)) out->duration_s = dur->valueint;
    if (cJSON_IsString(enc)) strncpy(out->encoding, enc->valuestring, sizeof(out->encoding) - 1);
}

bool media_client_configured(void)
{
    if (!media_configured()) {
        ESP_LOGW(TAG, "media service not configured: set PLAYER_MEDIA_SERVICE_URL "
                      "and PLAYER_MEDIA_API_KEY in menuconfig");
        return false;
    }
    return true;
}

esp_err_t media_import_query(const char *query, media_track_t *out, bool *out_created)
{
    if (!query || !*query) return ESP_ERR_INVALID_ARG;
    if (!media_configured()) return ESP_ERR_INVALID_STATE;

    // build {"query": "..."} with proper JSON escaping of quotes/backslashes
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root, "query", query);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;

    char url[256];
    snprintf(url, sizeof(url), "%s/tracks", MEDIA_BASE_URL);

    http_header_t headers[] = {
        { .key = "X-API-Key", .value = MEDIA_API_KEY },
    };

    http_response_t *resp = media_resp_alloc();
    if (!resp) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = http_post_with_timeout(url, body, "application/json",
                                           headers, 1, MEDIA_IMPORT_TIMEOUT_MS, resp);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "import POST failed: %s (status %d)", esp_err_to_name(err), resp->status_code);
        free(resp);
        return err;
    }

    if (resp->status_code == 401 || resp->status_code == 403) {
        ESP_LOGE(TAG, "import rejected (%d): check PLAYER_MEDIA_API_KEY", resp->status_code);
        free(resp);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (resp->status_code != 200 && resp->status_code != 201) {
        ESP_LOGE(TAG, "import failed (status %d): %.200s", resp->status_code, resp->data);
        free(resp);
        return ESP_FAIL;
    }

    if (out_created) *out_created = (resp->status_code == 201);

    esp_err_t result = ESP_OK;
    if (out) {
        cJSON *track = cJSON_Parse(resp->data);
        if (!track) {
            ESP_LOGE(TAG, "failed to parse import response: %.200s", resp->data);
            result = ESP_FAIL;
        } else {
            media_parse_track(track, out);
            cJSON_Delete(track);
        }
    }
    free(resp);
    return result;
}

esp_err_t media_list_tracks(int limit, int offset,
                            media_track_t *out_tracks, size_t max_tracks,
                            size_t *out_count, int *out_total)
{
    if (!out_tracks || !out_count || max_tracks == 0) return ESP_ERR_INVALID_ARG;
    if (!media_configured()) return ESP_ERR_INVALID_STATE;
    *out_count = 0;

    char url[256];
    if (limit > 0) {
        snprintf(url, sizeof(url), "%s/tracks?limit=%d&offset=%d", MEDIA_BASE_URL, limit, offset);
    } else {
        snprintf(url, sizeof(url), "%s/tracks", MEDIA_BASE_URL);
    }

    http_header_t headers[] = {
        { .key = "X-API-Key", .value = MEDIA_API_KEY },
    };

    http_response_t *resp = media_resp_alloc();
    if (!resp) return ESP_ERR_NO_MEM;

    esp_err_t err = http_get_with_timeout(url, headers, 1, MEDIA_LIST_TIMEOUT_MS, resp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "list GET failed: %s (status %d)", esp_err_to_name(err), resp->status_code);
        free(resp);
        return err;
    }

    if (resp->status_code == 401 || resp->status_code == 403) {
        ESP_LOGE(TAG, "list rejected (%d): check PLAYER_MEDIA_API_KEY", resp->status_code);
        free(resp);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (resp->status_code != 200) {
        ESP_LOGE(TAG, "list failed (status %d): %.200s", resp->status_code, resp->data);
        free(resp);
        return ESP_FAIL;
    }
    if (resp->truncated) {
        ESP_LOGW(TAG, "list response truncated — reduce limit");
    }

    cJSON *root = cJSON_Parse(resp->data);
    free(resp);
    if (!root) {
        ESP_LOGE(TAG, "failed to parse list response");
        return ESP_FAIL;
    }

    const cJSON *total = cJSON_GetObjectItem(root, "total");
    if (out_total && cJSON_IsNumber(total)) *out_total = total->valueint;

    const cJSON *items = cJSON_GetObjectItem(root, "tracks");
    if (cJSON_IsArray(items)) {
        const cJSON *item;
        cJSON_ArrayForEach(item, items) {
            if (*out_count >= max_tracks) break;
            media_parse_track(item, &out_tracks[*out_count]);
            (*out_count)++;
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "listed %u media tracks", (unsigned)*out_count);
    return ESP_OK;
}

esp_err_t media_stream_url(int track_id, char *out, size_t out_size)
{
    if (!out || out_size == 0) return ESP_ERR_INVALID_ARG;
    int written = snprintf(out, out_size, "%s/tracks/%d/stream", MEDIA_BASE_URL, track_id);
    if (written < 0 || written >= (int)out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}
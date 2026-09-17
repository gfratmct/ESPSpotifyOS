#include "net/spotify_auth.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <esp_log.h>
#include <mbedtls/base64.h>
#include <cJSON.h>

#include "data/auth_state.h"
#include "net/http_client.h"

#define TAG "spotify_auth"

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)

#define SPOTIFY_AUTHORIZE_URL "https://accounts.spotify.com/authorize"
#define SPOTIFY_TOKEN_URL     "https://accounts.spotify.com/api/token"

#define SPOTIFY_CLIENT_ID     CONFIG_PLAYER_SPOTIFY_CLIENT_ID
#define SPOTIFY_CLIENT_SECRET CONFIG_PLAYER_SPOTIFY_CLIENT_SECRET
#define SPOTIFY_REDIRECT_URI  "http://127.0.0.1:" STRINGIFY(CONFIG_PLAYER_WEBSERVER_PORT) "/callback"
#define SPOTIFY_SCOPES \
    "user-read-private user-read-email user-library-read user-read-recently-played " \
    "playlist-read-private user-read-playback-state user-modify-playback-state " \
    "user-read-currently-playing app-remote-control streaming"

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
    auth_state_t *state = auth_state_get();
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
    return auth_state_save();
}

// Parses {"access_token", "refresh_token"?, "expires_in"} from a token endpoint
// response and stores it into the auth state.
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

void spotify_logout(void)
{
    auth_state_t *state = auth_state_get();
    if (!state || !state->is_logged_in) {
        return; // no session to drop — no state change
    }
    ESP_LOGW(TAG, "Spotify session invalid — logging out");
    memset(state->spotify_token, 0, sizeof(state->spotify_token));
    memset(state->refresh_token, 0, sizeof(state->refresh_token));
    state->token_expires_at = 0;
    state->is_logged_in = false;
    auth_state_save(); // the ui_state loop switches to the login screen
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
    auth_state_t *state = auth_state_get();
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

#include "data/state.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "utils/storage.h"

#define TAG "app_state"
#define STATE_NAMESPACE "app_state"
#define STATE_BLOB_KEY "state_blob"

static app_state_t g_app_state;

// NVS blob layout from before the token buffers were enlarged to 512 bytes.
// Kept only so old state can be migrated instead of discarded.
typedef struct {
    uint8_t spotify_token[256];
    uint8_t refresh_token[256];
    uint8_t wifi_ssid[32];
    uint8_t wifi_password[64];
    uint32_t token_expires_at;
    bool is_logged_in;
    enum ScreensEnum current_screen;
} app_state_v1_t;

// Copies an old-layout blob field by field into the active state.
static void migrate_state_v1(const app_state_v1_t *old)
{
    memcpy(g_app_state.spotify_token, old->spotify_token, sizeof(old->spotify_token));
    memcpy(g_app_state.refresh_token, old->refresh_token, sizeof(old->refresh_token));
    memcpy(g_app_state.wifi_ssid, old->wifi_ssid, sizeof(old->wifi_ssid));
    memcpy(g_app_state.wifi_password, old->wifi_password, sizeof(old->wifi_password));
    g_app_state.token_expires_at = old->token_expires_at;
    g_app_state.is_logged_in = old->is_logged_in;
    g_app_state.current_screen = old->current_screen;
    // keep NUL-termination within the (larger) new buffers
    g_app_state.spotify_token[sizeof(g_app_state.spotify_token) - 1] = '\0';
    g_app_state.refresh_token[sizeof(g_app_state.refresh_token) - 1] = '\0';
}

void init_default_state(void)
{
    memset(&g_app_state, 0, sizeof(app_state_t));
    g_app_state.current_screen = SCREEN_ID_SETUP;
    g_app_state.is_logged_in = false;
    g_app_state.token_expires_at = 0;
    strncpy((char *)g_app_state.wifi_ssid, CONFIG_ESP_WIFI_SSID, sizeof(g_app_state.wifi_ssid) - 1);
    g_app_state.wifi_ssid[sizeof(g_app_state.wifi_ssid) - 1] = '\0';
    strncpy((char *)g_app_state.wifi_password, CONFIG_ESP_WIFI_PASSWORD, sizeof(g_app_state.wifi_password) - 1);
    g_app_state.wifi_password[sizeof(g_app_state.wifi_password) - 1] = '\0';

    sprintf((char *)g_app_state.spotify_token, "%s", "[TOKEN_NOT_INITIALIZED]");
    sprintf((char *)g_app_state.refresh_token, "%s", "[REFRESH_NOT_INITIALIZED]");
}

esp_err_t load_app_state(void)
{
    // populate RAM with known defaults
    init_default_state();

    // Read from NVS flash into a temporary struct. The blob may have been
    // written by an older firmware with a different (smaller) layout, so
    // decide based on the actual blob size.
    app_state_t loaded_state;
    size_t loaded_len = 0;
    esp_err_t err = storage_get_blob(STATE_NAMESPACE, STATE_BLOB_KEY,
                                     &loaded_state, sizeof(app_state_t), &loaded_len);

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No existing state found in flash — using defaults");
        return ESP_OK;
    }

    if (err == ESP_OK && loaded_len == sizeof(app_state_t))
    {
        // Valid state found: copy to active state
        memcpy(&g_app_state, &loaded_state, sizeof(app_state_t));
        ESP_LOGI(TAG, "State loaded successfully from flash (logged_in=%d, screen=%d)",
                 g_app_state.is_logged_in, g_app_state.current_screen);
        return ESP_OK;
    }

    if (err == ESP_OK && loaded_len == sizeof(app_state_v1_t))
    {
        // Old-layout blob: migrate so the login/refresh token survives
        migrate_state_v1((const app_state_v1_t *)&loaded_state);
        ESP_LOGI(TAG, "Migrated state from v1 blob layout (logged_in=%d)",
                 g_app_state.is_logged_in);
        save_app_state(); // persist in the new layout
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Flash state corrupt or version mismatch (0x%x) — using defaults", err);
    return err;
}

esp_err_t save_app_state(void)
{
    esp_err_t err = storage_set_blob(STATE_NAMESPACE, STATE_BLOB_KEY,
                                     &g_app_state, sizeof(app_state_t));
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "State saved to flash");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to save state to flash: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t clear_app_state(void)
{
    init_default_state();
    return storage_erase_namespace(STATE_NAMESPACE);
}

app_state_t *get_app_state(void)
{
    return &g_app_state;
}
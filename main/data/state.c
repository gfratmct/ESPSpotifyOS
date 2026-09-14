#include "data/state.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "utils/storage.h"

#define TAG "app_state"
#define STATE_NAMESPACE "app_state"
#define STATE_BLOB_KEY "state_blob"

static app_state_t g_app_state;

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

    // Read from NVS flash into a temporary buffer
    app_state_t loaded_state;
    size_t loaded_len = 0;
    esp_err_t err = storage_get_blob(STATE_NAMESPACE, STATE_BLOB_KEY,
                                     &loaded_state, sizeof(app_state_t), &loaded_len);

    if (err == ESP_OK && loaded_len == sizeof(app_state_t))
    {
        // Valid state found: copy to active state
        memcpy(&g_app_state, &loaded_state, sizeof(app_state_t));
        ESP_LOGI(TAG, "State loaded successfully from flash (logged_in=%d, screen=%d)",
                 g_app_state.is_logged_in, g_app_state.current_screen);
        return ESP_OK;
    }

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No existing state found in flash — using defaults");
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
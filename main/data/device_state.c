#include "data/device_state.h"

#include <string.h>

#include <esp_log.h>

#include "utils/storage.h"

#define TAG "device_state"
#define DEVICE_NAMESPACE "device"
#define DEVICE_BLOB_KEY  "blob"

static device_state_t g_device_state;

void device_state_reset(void)
{
    memset(&g_device_state, 0, sizeof(g_device_state));
    g_device_state.current_screen = SCREEN_ID_SETUP;

    strncpy((char *)g_device_state.wifi_ssid, CONFIG_ESP_WIFI_SSID,
            sizeof(g_device_state.wifi_ssid) - 1);
    g_device_state.wifi_ssid[sizeof(g_device_state.wifi_ssid) - 1] = '\0';

    strncpy((char *)g_device_state.wifi_password, CONFIG_ESP_WIFI_PASSWORD,
            sizeof(g_device_state.wifi_password) - 1);
    g_device_state.wifi_password[sizeof(g_device_state.wifi_password) - 1] = '\0';
}

esp_err_t device_state_load(void)
{
    size_t loaded_len = 0;
    device_state_t loaded;
    esp_err_t err = storage_get_blob(DEVICE_NAMESPACE, DEVICE_BLOB_KEY,
                                     &loaded, sizeof(loaded), &loaded_len);
    if (err != ESP_OK) {
        return err;
    }
    if (loaded_len != sizeof(loaded)) {
        ESP_LOGW(TAG, "Ignoring device blob of unexpected size %u", (unsigned)loaded_len);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(&g_device_state, &loaded, sizeof(g_device_state));
    ESP_LOGI(TAG, "Device state loaded (screen=%d)", g_device_state.current_screen);
    return ESP_OK;
}

esp_err_t device_state_save(void)
{
    return storage_set_blob(DEVICE_NAMESPACE, DEVICE_BLOB_KEY,
                            &g_device_state, sizeof(g_device_state));
}

esp_err_t device_state_reset_wifi(void)
{
    memset(g_device_state.wifi_ssid, 0, sizeof(g_device_state.wifi_ssid));
    memset(g_device_state.wifi_password, 0, sizeof(g_device_state.wifi_password));

    strncpy((char *)g_device_state.wifi_ssid, CONFIG_ESP_WIFI_SSID,
            sizeof(g_device_state.wifi_ssid) - 1);
    strncpy((char *)g_device_state.wifi_password, CONFIG_ESP_WIFI_PASSWORD,
            sizeof(g_device_state.wifi_password) - 1);

    return device_state_save();
}

device_state_t *device_state_get(void)
{
    return &g_device_state;
}

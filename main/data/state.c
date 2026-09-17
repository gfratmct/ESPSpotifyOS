#include "data/state.h"

#include <string.h>

#include <esp_log.h>
#include <nvs.h>

#include "data/auth_state.h"
#include "data/device_state.h"
#include "utils/storage.h"

#define TAG "state"

// Legacy combined blob (namespace/key) written by firmware before the
// auth/device split. Kept only for migration.
#define LEGACY_NAMESPACE "app_state"
#define LEGACY_BLOB_KEY  "state_blob"

// Current legacy layout: 512-byte token buffers.
typedef struct {
    uint8_t spotify_token[512];
    uint8_t refresh_token[512];
    uint8_t wifi_ssid[32];
    uint8_t wifi_password[64];
    uint32_t token_expires_at;
    bool is_logged_in;
    enum ScreensEnum current_screen;
} legacy_state_v1_t;

// Original layout: 256-byte token buffers.
typedef struct {
    uint8_t spotify_token[256];
    uint8_t refresh_token[256];
    uint8_t wifi_ssid[32];
    uint8_t wifi_password[64];
    uint32_t token_expires_at;
    bool is_logged_in;
    enum ScreensEnum current_screen;
} legacy_state_v0_t;

// Copies a legacy blob (either layout) into the split auth/device state and
// persists it. No-op when no legacy blob exists.
static void migrate_legacy_blob(void)
{
    union {
        legacy_state_v1_t v1;
        legacy_state_v0_t v0;
    } legacy;
    size_t len = 0;
    esp_err_t err = storage_get_blob(LEGACY_NAMESPACE, LEGACY_BLOB_KEY,
                                     &legacy, sizeof(legacy), &len);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NOT_FOUND) {
        return; // fresh install — nothing to migrate
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not read legacy state: %s", esp_err_to_name(err));
        return;
    }

    auth_state_t *auth = auth_state_get();
    device_state_t *device = device_state_get();

    const uint8_t *token = NULL, *refresh = NULL, *ssid = NULL, *password = NULL;
    uint32_t expires_at = 0;
    bool logged_in = false;
    enum ScreensEnum screen = SCREEN_ID_SETUP;

    if (len == sizeof(legacy_state_v1_t)) {
        token = legacy.v1.spotify_token;
        refresh = legacy.v1.refresh_token;
        expires_at = legacy.v1.token_expires_at;
        logged_in = legacy.v1.is_logged_in;
        ssid = legacy.v1.wifi_ssid;
        password = legacy.v1.wifi_password;
        screen = legacy.v1.current_screen;
    } else if (len == sizeof(legacy_state_v0_t)) {
        token = legacy.v0.spotify_token;
        refresh = legacy.v0.refresh_token;
        expires_at = legacy.v0.token_expires_at;
        logged_in = legacy.v0.is_logged_in;
        ssid = legacy.v0.wifi_ssid;
        password = legacy.v0.wifi_password;
        screen = legacy.v0.current_screen;
    } else {
        ESP_LOGW(TAG, "Legacy state has unexpected size %u — discarding", (unsigned)len);
        storage_erase_namespace(LEGACY_NAMESPACE);
        return;
    }

    // token/refresh may be 256-byte v0 buffers; only copy what each source holds
    size_t token_src_len = (len == sizeof(legacy_state_v0_t)) ? 256 : 512;
    memcpy(auth->spotify_token, token, token_src_len);
    memcpy(auth->refresh_token, refresh, token_src_len);
    auth->token_expires_at = expires_at;
    auth->is_logged_in = logged_in;
    // keep NUL-termination within the (larger) new buffers
    auth->spotify_token[sizeof(auth->spotify_token) - 1] = '\0';
    auth->refresh_token[sizeof(auth->refresh_token) - 1] = '\0';

    memcpy(device->wifi_ssid, ssid, sizeof(device->wifi_ssid));
    memcpy(device->wifi_password, password, sizeof(device->wifi_password));
    device->current_screen = screen;

    auth_state_save();
    device_state_save();
    storage_erase_namespace(LEGACY_NAMESPACE);
    ESP_LOGI(TAG, "Migrated legacy app_state blob (logged_in=%d)", logged_in);
}

esp_err_t state_init(void)
{
    auth_state_reset();
    device_state_reset();

    bool auth_present = (auth_state_load() == ESP_OK);
    bool device_present = (device_state_load() == ESP_OK);

    // Upgrade path: no split state yet, but a legacy combined blob may exist.
    if (!auth_present && !device_present) {
        migrate_legacy_blob();
    }
    return ESP_OK;
}

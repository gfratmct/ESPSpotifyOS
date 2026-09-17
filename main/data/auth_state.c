#include "data/auth_state.h"

#include <string.h>

#include <esp_log.h>

#include "utils/storage.h"

#define TAG "auth_state"
#define AUTH_NAMESPACE "auth"
#define AUTH_BLOB_KEY  "blob"

static auth_state_t g_auth_state;

void auth_state_reset(void)
{
    memset(&g_auth_state, 0, sizeof(g_auth_state));
}

esp_err_t auth_state_load(void)
{
    size_t loaded_len = 0;
    auth_state_t loaded;
    esp_err_t err = storage_get_blob(AUTH_NAMESPACE, AUTH_BLOB_KEY,
                                     &loaded, sizeof(loaded), &loaded_len);
    if (err != ESP_OK) {
        return err;
    }
    if (loaded_len != sizeof(loaded)) {
        ESP_LOGW(TAG, "Ignoring auth blob of unexpected size %u", (unsigned)loaded_len);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(&g_auth_state, &loaded, sizeof(g_auth_state));
    ESP_LOGI(TAG, "Auth state loaded (logged_in=%d)", g_auth_state.is_logged_in);
    return ESP_OK;
}

esp_err_t auth_state_save(void)
{
    return storage_set_blob(AUTH_NAMESPACE, AUTH_BLOB_KEY,
                            &g_auth_state, sizeof(g_auth_state));
}

auth_state_t *auth_state_get(void)
{
    return &g_auth_state;
}

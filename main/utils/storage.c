#include "utils/storage.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG "storage"

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated/new version, erasing...");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "failed to erase NVS flash");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t storage_set_blob(const char *namespace_name, const char *key, const void *data, size_t len)
{
    if (!namespace_name || !key || !data || len == 0) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_get_blob(const char *namespace_name, const char *key, void *out_data, size_t max_len, size_t *out_len)
{
    if (!namespace_name || !key || !out_data || max_len == 0) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t required_size = max_len;
    err = nvs_get_blob(handle, key, out_data, &required_size);
    if (err == ESP_OK && out_len) {
        *out_len = required_size;
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_erase_namespace(const char *namespace_name)
{
    if (!namespace_name) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

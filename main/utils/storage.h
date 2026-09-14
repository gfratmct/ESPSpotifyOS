#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_check.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize NVS storage subsystem.
 */
esp_err_t storage_init(void);

/**
 * @brief Write a string value to NVS under a given namespace.
 */
esp_err_t storage_set_str(const char *namespace_name, const char *key, const char *value);

/**
 * @brief Read a string value from NVS under a given namespace.
 * @param out_val Buffer to write output into.
 * @param max_len Size of out_val buffer.
 */
esp_err_t storage_get_str(const char *namespace_name, const char *key, char *out_val, size_t max_len);

/**
 * @brief Write integer values to NVS.
 */
esp_err_t storage_set_i32(const char *namespace_name, const char *key, int32_t value);
esp_err_t storage_get_i32(const char *namespace_name, const char *key, int32_t *out_val);

esp_err_t storage_set_u32(const char *namespace_name, const char *key, uint32_t value);
esp_err_t storage_get_u32(const char *namespace_name, const char *key, uint32_t *out_val);

/**
 * @brief Write raw binary / struct data blob to NVS.
 */
esp_err_t storage_set_blob(const char *namespace_name, const char *key, const void *data, size_t len);

/**
 * @brief Read raw binary / struct data blob from NVS.
 */
esp_err_t storage_get_blob(const char *namespace_name, const char *key, void *out_data, size_t max_len, size_t *out_len);

/**
 * @brief Erase a specific key from a namespace.
 */
esp_err_t storage_erase_key(const char *namespace_name, const char *key);

/**
 * @brief Erase all keys in a given namespace.
 */
esp_err_t storage_erase_namespace(const char *namespace_name);

#ifdef __cplusplus
}
#endif
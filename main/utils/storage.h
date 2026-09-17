#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the NVS storage subsystem, erasing it first if the
 *        partition is full or was written by an incompatible version.
 */
esp_err_t storage_init(void);

/**
 * @brief Write raw binary / struct data blob to NVS.
 */
esp_err_t storage_set_blob(const char *namespace_name, const char *key, const void *data, size_t len);

/**
 * @brief Read raw binary / struct data blob from NVS.
 * @param out_len Optional; receives the number of bytes actually read.
 */
esp_err_t storage_get_blob(const char *namespace_name, const char *key, void *out_data, size_t max_len, size_t *out_len);

/**
 * @brief Erase all keys in a given namespace.
 */
esp_err_t storage_erase_namespace(const char *namespace_name);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stddef.h>
#include "esp_err.h"

// Placeholder sink for persisting streamed track data as a file on the SD card.
//
// The real implementation will mount the SD slot (SPI, CS = CONFIG_PLAYER_SD_CS_GPIO,
// sharing the LCD bus) and write into CONFIG_PLAYER_SD_MOUNT_POINT + CONFIG_PLAYER_MUSIC_DIR.
// Until that exists these calls only track the byte count so the streaming path can be
// exercised end to end without storage hardware. NVS is not an option here: its partition
// is far too small for multi-megabyte track blobs.

/**
 * @brief Begin a blob for `track_id` (encoding used as the file extension).
 */
esp_err_t blob_store_open(int track_id, const char *encoding);

/**
 * @brief Append `len` bytes of streamed data to the current blob.
 */
esp_err_t blob_store_write(const void *data, size_t len);

/**
 * @brief Finish the current blob.
 */
esp_err_t blob_store_close(void);

/**
 * @brief Bytes appended since the last blob_store_open().
 */
size_t blob_store_bytes_written(void);

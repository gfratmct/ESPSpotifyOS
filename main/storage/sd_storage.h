#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// A media file found in the on-SD music directory.
typedef struct {
    char name[256];       // file name (basename)
    char path[576];       // full VFS path
    int size_bytes;
} sd_file_t;

/**
 * @brief Mount the SD card over SPI (shares the LCD SPI bus, which must be
 *        initialized first). Idempotent; returns the first error if mounting
 *        fails (e.g. no card inserted).
 */
esp_err_t sd_storage_mount(void);

/**
 * @brief Whether the SD card is currently mounted.
 */
bool sd_storage_ready(void);

// Mount point and music directory (from Kconfig).
const char *sd_storage_mount_point(void);
const char *sd_storage_music_dir(void);

// Creates the music directory if missing. ESP_ERR_INVALID_STATE if not mounted.
esp_err_t sd_storage_ensure_music_dir(void);

/**
 * @brief Lists audio files in the music directory, paging over `offset`/`limit`.
 *        `out_total` receives the total number of matching files.
 */
esp_err_t sd_storage_list_music(int offset, int limit, sd_file_t *out, size_t max,
                                size_t *out_count, int *out_total);

#ifdef __cplusplus
}
#endif

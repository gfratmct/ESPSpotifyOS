#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stream a media-server track into the SD cache.
 *
 * `display_name` ("artist - title") is sanitized and used as the file name.
 * Returns ESP_OK when the track is cached; *created is true when a new file was
 * written, false when it was already present. Returns ESP_ERR_INVALID_STATE
 * when SD storage isn't available.
 */
esp_err_t track_cache_fetch(int track_id, const char *encoding,
                            const char *display_name, bool *created);

#ifdef __cplusplus
}
#endif

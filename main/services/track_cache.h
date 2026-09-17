#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque streaming writer into the SD track cache.
typedef struct track_cache_writer track_cache_writer_t;

// Whether the SD cache is available (SD mounted).
bool track_cache_available(void);

// Builds the sanitized cache path for a display name / encoding.
esp_err_t track_cache_build_path(char *out, size_t out_size,
                                 const char *display_name, const char *encoding);

// Whether a cached file already exists for this name / encoding.
bool track_cache_exists(const char *display_name, const char *encoding);

/**
 * @brief Opens a writer for `display_name` / `encoding`.
 *
 * Returns NULL when SD storage is unavailable or the file already exists (in
 * which case *already_cached is set true). Call track_cache_end() or
 * track_cache_abort() to release the returned writer.
 */
track_cache_writer_t *track_cache_begin(const char *display_name, const char *encoding,
                                        bool *already_cached);

// Appends bytes to the cache file.
esp_err_t track_cache_write(track_cache_writer_t *writer, const void *data, size_t len);

// Finalizes the writer, keeping the cached file. Frees the writer.
esp_err_t track_cache_end(track_cache_writer_t *writer);

// Discards the writer, deleting the partial file. Frees the writer.
void track_cache_abort(track_cache_writer_t *writer);

/**
 * @brief Stream a whole media-server track into the cache (blocking).
 * @return ESP_OK on success (also when already cached); ESP_ERR_INVALID_STATE
 *         when SD storage isn't available; other errors on download failure.
 */
esp_err_t track_cache_fetch(int track_id, const char *encoding,
                            const char *display_name, bool *created);

#ifdef __cplusplus
}
#endif

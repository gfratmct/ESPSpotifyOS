#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// A track as returned by the media service (a subset of the server's payload,
// trimmed to what the ESP32 needs).
typedef struct {
    int id;
    char title[128];
    char artist[128];
    int duration_s;
    char encoding[16]; // mp3, wav, aac, flac, ogg, opus, ...
} media_track_t;

/**
 * @brief True when a media service URL and API key are configured in Kconfig.
 */
bool media_client_configured(void);

/**
 * @brief Import a track into the media service by searching for it.
 *
 * The service searches YouTube Music for `query` and imports the first match.
 * Blocks for the duration of the download (can take a minute) — call from a
 * background task, never from the LVGL task.
 *
 * @param query        search query, e.g. "artist - title"
 * @param out          parsed imported track (may be NULL)
 * @param out_created  set true when the track was newly imported, false when
 *                     it was already in the library (may be NULL)
 */
esp_err_t media_import_query(const char *query, media_track_t *out, bool *out_created);

/**
 * @brief Fetch a page of the media service's imported tracks.
 *
 * @param limit       max items in this page (<= 0 = all)
 * @param offset      paging offset
 * @param out_tracks  caller-provided array of at least max_tracks
 * @param max_tracks  capacity of out_tracks
 * @param out_count   actual number of tracks parsed
 * @param out_total   total tracks in the library (may be NULL)
 */
esp_err_t media_list_tracks(int limit, int offset,
                            media_track_t *out_tracks, size_t max_tracks,
                            size_t *out_count, int *out_total);

/**
 * @brief Build the URL used to stream a track (for the future audio path).
 */
esp_err_t media_stream_url(int track_id, char *out, size_t out_size);
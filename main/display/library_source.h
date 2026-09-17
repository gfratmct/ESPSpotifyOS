#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// A single browsable entry, normalized across sources (Spotify liked tracks,
// media-server library, SD cache).
typedef struct {
    char title[128];
    char subtitle[128];        // artist
    int duration_s;            // -1 when unknown
    int id;                    // media-server track id, -1 when none
    char encoding[16];         // media encoding, for downloads
    char transfer_key[288];    // import query ("artist - title") or local path
} library_item_t;

typedef enum {
    TRANSFER_NONE = 0,   // source/item has no long-press action
    TRANSFER_STARTED,
    TRANSFER_BUSY,       // another transfer is already running
    TRANSFER_UNAVAILABLE // transfer target not configured
} transfer_request_result_t;

// A collection the library list can page through. Implementations live in
// library_sources.c.
typedef struct library_source {
    const char *name;           // tab label
    const char *empty_message;  // shown when the first page is empty
    // Fetches up to `max` items starting at `offset`. Must fill *out_count and,
    // when known, *out_total. Returns ESP_OK on success.
    esp_err_t (*fetch)(int offset, int limit, library_item_t *out, size_t max,
                       size_t *out_count, int *out_total);
    // Long-press action for an item (e.g. import to server). NULL when the
    // source has no long-press action.
    transfer_request_result_t (*start_transfer)(const library_item_t *item);
} library_source_t;

// Built-in sources.
const library_source_t *library_source_spotify(void);
const library_source_t *library_source_server(void);
const library_source_t *library_source_sd(void);

#ifdef __cplusplus
}
#endif

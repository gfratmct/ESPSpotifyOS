#include "display/library_source.h"

#include <stdio.h>
#include <string.h>

#include "net/media_client.h"
#include "net/spotify_api.h"
#include "services/transfer_manager.h"
#include "storage/sd_storage.h"

#define SOURCE_MAX_PAGE 50

// ---- Spotify liked tracks (import source) -----------------------------------

static esp_err_t spotify_fetch(int offset, int limit, library_item_t *out, size_t max,
                               size_t *out_count, int *out_total)
{
    static spotify_track_t tracks[SOURCE_MAX_PAGE];
    if (limit > SOURCE_MAX_PAGE) limit = SOURCE_MAX_PAGE;

    size_t count = 0;
    esp_err_t err = spotify_get_saved_tracks(limit, offset, NULL, tracks, limit, &count, out_total);
    if (err != ESP_OK) return err;

    if (count > max) count = max;
    for (size_t i = 0; i < count; i++) {
        library_item_t *it = &out[i];
        memset(it, 0, sizeof(*it));
        it->id = -1;
        it->duration_s = tracks[i].duration_ms > 0 ? tracks[i].duration_ms / 1000 : -1;
        strncpy(it->title, tracks[i].track_name, sizeof(it->title) - 1);
        strncpy(it->subtitle, tracks[i].track_artist, sizeof(it->subtitle) - 1);
        snprintf(it->transfer_key, sizeof(it->transfer_key), "%s - %s",
                 tracks[i].track_artist, tracks[i].track_name);
    }
    *out_count = count;
    return ESP_OK;
}

static transfer_request_result_t spotify_start_transfer(const library_item_t *item)
{
    if (!media_client_configured()) return TRANSFER_UNAVAILABLE;
    return transfer_manager_import(item->transfer_key) ? TRANSFER_STARTED : TRANSFER_BUSY;
}

static const library_source_t s_spotify_source = {
    .name = "Spotify",
    .empty_message = "No liked songs yet",
    .fetch = spotify_fetch,
    .start_transfer = spotify_start_transfer,
};

// ---- Media-server library ---------------------------------------------------

static esp_err_t server_fetch(int offset, int limit, library_item_t *out, size_t max,
                              size_t *out_count, int *out_total)
{
    static media_track_t tracks[SOURCE_MAX_PAGE];
    if (limit > SOURCE_MAX_PAGE) limit = SOURCE_MAX_PAGE;

    size_t count = 0;
    esp_err_t err = media_list_tracks(limit, offset, tracks, limit, &count, out_total);
    if (err != ESP_OK) return err;

    if (count > max) count = max;
    for (size_t i = 0; i < count; i++) {
        library_item_t *it = &out[i];
        memset(it, 0, sizeof(*it));
        it->id = tracks[i].id;
        it->duration_s = tracks[i].duration_s;
        strncpy(it->title, tracks[i].title, sizeof(it->title) - 1);
        strncpy(it->subtitle, tracks[i].artist, sizeof(it->subtitle) - 1);
        strncpy(it->encoding, tracks[i].encoding, sizeof(it->encoding) - 1);
        snprintf(it->transfer_key, sizeof(it->transfer_key), "%d", tracks[i].id);
    }
    *out_count = count;
    return ESP_OK;
}

static transfer_request_result_t server_start_transfer(const library_item_t *item)
{
    if (!media_client_configured() || !sd_storage_ready()) return TRANSFER_UNAVAILABLE;

    char display_name[288];
    snprintf(display_name, sizeof(display_name), "%s - %s", item->subtitle, item->title);
    return transfer_manager_download(item->id, item->encoding, display_name)
               ? TRANSFER_STARTED : TRANSFER_BUSY;
}

static const library_source_t s_server_source = {
    .name = "Server",
    .empty_message = "Server library empty",
    .fetch = server_fetch,
    .start_transfer = server_start_transfer,
};

// ---- Local SD cache (offline) ------------------------------------------------

// Splits a cached file stem ("artist - title") into title/subtitle.
static void split_stem(const char *stem, library_item_t *it)
{
    const char *sep = strstr(stem, " - ");
    if (sep) {
        size_t artist_len = (size_t)(sep - stem);
        if (artist_len >= sizeof(it->subtitle)) artist_len = sizeof(it->subtitle) - 1;
        memcpy(it->subtitle, stem, artist_len);
        it->subtitle[artist_len] = '\0';
        strncpy(it->title, sep + 3, sizeof(it->title) - 1);
    } else {
        strncpy(it->title, stem, sizeof(it->title) - 1);
    }
}

static esp_err_t sd_fetch(int offset, int limit, library_item_t *out, size_t max,
                          size_t *out_count, int *out_total)
{
    if (out_count) *out_count = 0;
    if (out_total) *out_total = 0;

    if (!sd_storage_ready()) {
        return ESP_OK; // list shows the source's empty message
    }

    static sd_file_t files[SOURCE_MAX_PAGE];
    if (limit > SOURCE_MAX_PAGE) limit = SOURCE_MAX_PAGE;

    size_t count = 0;
    esp_err_t err = sd_storage_list_music(offset, limit, files, limit, &count, out_total);
    if (err != ESP_OK) return err;

    if (count > max) count = max;
    for (size_t i = 0; i < count; i++) {
        library_item_t *it = &out[i];
        memset(it, 0, sizeof(*it));
        it->id = -1;
        it->duration_s = -1;

        char stem[160];
        strncpy(stem, files[i].name, sizeof(stem) - 1);
        stem[sizeof(stem) - 1] = '\0';
        char *dot = strrchr(stem, '.');
        if (dot) *dot = '\0';

        split_stem(stem, it);
        strncpy(it->transfer_key, files[i].path, sizeof(it->transfer_key) - 1);
    }
    *out_count = count;
    return ESP_OK;
}

static const library_source_t s_sd_source = {
    .name = "SD",
    .empty_message = "SD storage empty",
    .fetch = sd_fetch,
    .start_transfer = NULL,
};

// ---- accessors ---------------------------------------------------------------

const library_source_t *library_source_spotify(void) { return &s_spotify_source; }
const library_source_t *library_source_server(void) { return &s_server_source; }
const library_source_t *library_source_sd(void) { return &s_sd_source; }

#include "services/track_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <esp_log.h>
#include <esp_timer.h>

#include "net/http_client.h"
#include "net/media_client.h"
#include "storage/sd_storage.h"

#define TAG "track_cache"

#define CACHE_TIMEOUT_MS 120000

struct track_cache_writer {
    FILE *file;
    char path[288];
    size_t bytes;
    int64_t started_us;
};

static const char *extension_or_default(const char *encoding)
{
    return (encoding && encoding[0]) ? encoding : "bin";
}

// Replaces characters that are invalid in FAT file names.
static void sanitize_name(char *name)
{
    for (; *name; name++) {
        switch (*name) {
        case '/': case '\\': case ':': case '*': case '?':
        case '"': case '<': case '>': case '|':
            *name = '_';
            break;
        default:
            break;
        }
    }
}

bool track_cache_available(void)
{
    // Passive: the card is mounted on explicit SD use (SD tab / download), not
    // implicitly during streaming, so playback startup stays allocation-free.
    return sd_storage_ready();
}

esp_err_t track_cache_build_path(char *out, size_t out_size,
                                 const char *display_name, const char *encoding)
{
    if (!out || out_size == 0) return ESP_ERR_INVALID_ARG;

    char name[192];
    snprintf(name, sizeof(name), "%s", (display_name && display_name[0]) ? display_name : "track");
    sanitize_name(name);

    int written = snprintf(out, out_size, "%s%s/%s.%s",
                           sd_storage_mount_point(), sd_storage_music_dir(),
                           name, extension_or_default(encoding));
    if (written < 0 || written >= (int)out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

bool track_cache_exists(const char *display_name, const char *encoding)
{
    char path[288];
    if (track_cache_build_path(path, sizeof(path), display_name, encoding) != ESP_OK) {
        return false;
    }
    struct stat st;
    return stat(path, &st) == 0;
}

track_cache_writer_t *track_cache_begin(const char *display_name, const char *encoding,
                                        bool *already_cached)
{
    if (already_cached) *already_cached = false;
    if (!track_cache_available()) return NULL;
    if (sd_storage_ensure_music_dir() != ESP_OK) return NULL;

    char path[288];
    if (track_cache_build_path(path, sizeof(path), display_name, encoding) != ESP_OK) {
        return NULL;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        if (already_cached) *already_cached = true;
        return NULL;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "cannot create %s", path);
        return NULL;
    }

    track_cache_writer_t *writer = calloc(1, sizeof(*writer));
    if (!writer) {
        fclose(file);
        unlink(path);
        return NULL;
    }

    writer->file = file;
    strncpy(writer->path, path, sizeof(writer->path) - 1);
    writer->started_us = esp_timer_get_time();
    return writer;
}

esp_err_t track_cache_write(track_cache_writer_t *writer, const void *data, size_t len)
{
    if (!writer || !writer->file || !data || len == 0) return ESP_ERR_INVALID_ARG;

    if (fwrite(data, 1, len, writer->file) != len) {
        ESP_LOGW(TAG, "short write to %s", writer->path);
        return ESP_FAIL;
    }
    writer->bytes += len;
    return ESP_OK;
}

esp_err_t track_cache_end(track_cache_writer_t *writer)
{
    if (!writer) return ESP_ERR_INVALID_ARG;

    esp_err_t err = fflush(writer->file) == 0 ? ESP_OK : ESP_FAIL;
    fclose(writer->file);

    int64_t elapsed_us = esp_timer_get_time() - writer->started_us;
    double kb_per_s = elapsed_us > 0 ? (writer->bytes / 1024.0) / (elapsed_us / 1000000.0) : 0.0;
    ESP_LOGI(TAG, "cached %u bytes to %s (%.1f kB/s)",
             (unsigned)writer->bytes, writer->path, kb_per_s);

    free(writer);
    return err;
}

void track_cache_abort(track_cache_writer_t *writer)
{
    if (!writer) return;
    if (writer->file) {
        fclose(writer->file);
    }
    unlink(writer->path); // don't leave a partial file behind
    free(writer);
}

// ---- convenience: full download ---------------------------------------------

typedef struct {
    track_cache_writer_t *writer;
    bool write_failed;
} fetch_ctx_t;

static bool fetch_chunk_cb(void *ctx, const char *data, size_t len)
{
    fetch_ctx_t *fc = (fetch_ctx_t *)ctx;
    if (track_cache_write(fc->writer, data, len) != ESP_OK) {
        fc->write_failed = true;
        return false; // abort the transfer
    }
    return true;
}

esp_err_t track_cache_fetch(int track_id, const char *encoding,
                            const char *display_name, bool *created)
{
    if (created) *created = false;
    if (!track_cache_available()) return ESP_ERR_INVALID_STATE;

    bool already = false;
    track_cache_writer_t *writer = track_cache_begin(display_name, encoding, &already);
    if (!writer) {
        return already ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    char url[256];
    if (media_stream_url(track_id, url, sizeof(url)) != ESP_OK) {
        track_cache_abort(writer);
        return ESP_ERR_INVALID_ARG;
    }

    http_header_t headers[] = {
        { .key = "X-API-Key", .value = CONFIG_PLAYER_MEDIA_API_KEY },
    };

    fetch_ctx_t ctx = { .writer = writer };
    ESP_LOGI(TAG, "caching track %d", track_id);
    esp_err_t err = http_get_stream(url, headers, 1, CACHE_TIMEOUT_MS, fetch_chunk_cb, &ctx, NULL);

    if (err != ESP_OK || ctx.write_failed) {
        track_cache_abort(writer);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    track_cache_end(writer);
    if (created) *created = true;
    return ESP_OK;
}

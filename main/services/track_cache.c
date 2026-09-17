#include "services/track_cache.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <esp_log.h>
#include <esp_timer.h>

#include "net/http_client.h"
#include "net/media_client.h"
#include "storage/sd_storage.h"

#define TAG "track_cache"

#define CACHE_TIMEOUT_MS    120000
#define CACHE_HEX_BYTES     32
#define CACHE_PROGRESS_BYTES (32 * 1024)
#define CACHE_VERBOSE_CHUNKS 10

typedef struct {
    FILE *file;
    uint32_t chunk_count;
    size_t total_bytes;
    size_t next_progress;
    int64_t started_us;
    bool first_chunk_logged;
} cache_sink_t;

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

static void build_path(char *out, size_t out_size, const char *display_name,
                       const char *encoding)
{
    char name[192];
    snprintf(name, sizeof(name), "%s", (display_name && display_name[0]) ? display_name : "track");
    sanitize_name(name);
    snprintf(out, out_size, "%s%s/%s.%s", sd_storage_mount_point(), sd_storage_music_dir(),
             name, extension_or_default(encoding));
}

static bool chunk_cb(void *ctx, const char *data, size_t len)
{
    cache_sink_t *sink = (cache_sink_t *)ctx;

    sink->chunk_count++;
    sink->total_bytes += len;

    if (!sink->first_chunk_logged) {
        sink->first_chunk_logged = true;
        size_t hex_len = len < CACHE_HEX_BYTES ? len : CACHE_HEX_BYTES;
        ESP_LOGI(TAG, "first chunk: %u bytes", (unsigned)len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, hex_len, ESP_LOG_INFO);
    }

    bool verbose = sink->chunk_count <= CACHE_VERBOSE_CHUNKS;
    if (verbose || sink->total_bytes >= sink->next_progress) {
        int64_t elapsed_us = esp_timer_get_time() - sink->started_us;
        double kb_per_s = elapsed_us > 0
                              ? (sink->total_bytes / 1024.0) / (elapsed_us / 1000000.0)
                              : 0.0;
        ESP_LOGI(TAG, "chunk #%u: %u B, total %u B, %.1f kB/s",
                 (unsigned)sink->chunk_count, (unsigned)len,
                 (unsigned)sink->total_bytes, kb_per_s);
        while (sink->next_progress <= sink->total_bytes) {
            sink->next_progress += CACHE_PROGRESS_BYTES;
        }
    }

    if (fwrite(data, 1, len, sink->file) != len) {
        ESP_LOGW(TAG, "short write to cache");
        return false; // abort the transfer
    }
    return true;
}

esp_err_t track_cache_fetch(int track_id, const char *encoding,
                            const char *display_name, bool *created)
{
    if (created) *created = false;
    if (!sd_storage_ready()) return ESP_ERR_INVALID_STATE;
    if (sd_storage_ensure_music_dir() != ESP_OK) return ESP_ERR_INVALID_STATE;

    char path[288];
    build_path(path, sizeof(path), display_name, encoding);

    struct stat st;
    if (stat(path, &st) == 0) {
        ESP_LOGI(TAG, "already cached: %s", path);
        return ESP_OK;
    }

    char url[256];
    if (media_stream_url(track_id, url, sizeof(url)) != ESP_OK) {
        ESP_LOGE(TAG, "failed to build stream URL for track %d", track_id);
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "cannot create %s", path);
        return ESP_FAIL;
    }

    cache_sink_t sink = {
        .file = file,
        .next_progress = CACHE_PROGRESS_BYTES,
        .started_us = esp_timer_get_time(),
    };

    http_header_t headers[] = {
        { .key = "X-API-Key", .value = CONFIG_PLAYER_MEDIA_API_KEY },
    };

    ESP_LOGI(TAG, "caching track %d -> %s", track_id, path);
    esp_err_t err = http_get_stream(url, headers, 1, CACHE_TIMEOUT_MS, chunk_cb, &sink, NULL);
    fclose(file);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "download failed: %s", esp_err_to_name(err));
        unlink(path); // don't leave a partial file behind
        return err;
    }

    int64_t elapsed_us = esp_timer_get_time() - sink.started_us;
    double kb_per_s = elapsed_us > 0
                          ? (sink.total_bytes / 1024.0) / (elapsed_us / 1000000.0)
                          : 0.0;
    ESP_LOGI(TAG, "cached %u bytes in %u chunks (%.1f kB/s)",
             (unsigned)sink.total_bytes, (unsigned)sink.chunk_count, kb_per_s);

    if (created) *created = true;
    return ESP_OK;
}

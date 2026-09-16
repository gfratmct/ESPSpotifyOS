#include "utils/stream_debug.h"

#include <stdbool.h>
#include <stdint.h>

#include <esp_log.h>
#include <esp_timer.h>

#include "utils/blob_store.h"
#include "utils/http.h"
#include "utils/media.h"

#define TAG "stream_debug"

#define STREAM_DEBUG_TIMEOUT_MS    30000
#define STREAM_DEBUG_VERBOSE_CHUNKS 10            // log every chunk for the first N
#define STREAM_DEBUG_PROGRESS_BYTES (32 * 1024)   // then one line per this many bytes
#define STREAM_DEBUG_HEX_BYTES      32

typedef struct {
    uint32_t chunk_count;
    size_t total_bytes;
    size_t min_chunk;
    size_t max_chunk;
    size_t next_progress;
    int64_t started_us;
    bool first_chunk_logged;
} stream_stats_t;

static bool stream_chunk_cb(void *ctx, const char *data, size_t len)
{
    stream_stats_t *st = (stream_stats_t *)ctx;

    st->chunk_count++;
    st->total_bytes += len;
    if (st->min_chunk == 0 || len < st->min_chunk) st->min_chunk = len;
    if (len > st->max_chunk) st->max_chunk = len;

    if (!st->first_chunk_logged) {
        st->first_chunk_logged = true;
        size_t hex_len = len < STREAM_DEBUG_HEX_BYTES ? len : STREAM_DEBUG_HEX_BYTES;
        ESP_LOGI(TAG, "first chunk: %u bytes, leading %u bytes:",
                 (unsigned)len, (unsigned)hex_len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, hex_len, ESP_LOG_INFO);
    }

    bool verbose = st->chunk_count <= STREAM_DEBUG_VERBOSE_CHUNKS;
    if (verbose || st->total_bytes >= st->next_progress) {
        int64_t elapsed_us = esp_timer_get_time() - st->started_us;
        double kb_per_s = elapsed_us > 0
                              ? (st->total_bytes / 1024.0) / (elapsed_us / 1000000.0)
                              : 0.0;
        ESP_LOGI(TAG, "chunk #%u: %u B, total %u B, %.1f kB/s",
                 (unsigned)st->chunk_count, (unsigned)len, (unsigned)st->total_bytes, kb_per_s);
        while (st->next_progress <= st->total_bytes) {
            st->next_progress += STREAM_DEBUG_PROGRESS_BYTES;
        }
    }

    if (blob_store_write(data, len) != ESP_OK) {
        ESP_LOGW(TAG, "blob store write failed");
    }

    return true;
}

void stream_debug_track(int track_id, const char *encoding)
{
    if (!media_client_configured()) {
        ESP_LOGW(TAG, "media service not configured, skipping stream debug");
        return;
    }

    char url[256];
    if (media_stream_url(track_id, url, sizeof(url)) != ESP_OK) {
        ESP_LOGE(TAG, "failed to build stream URL for track %d", track_id);
        return;
    }

    http_header_t headers[] = {
        { .key = "X-API-Key", .value = CONFIG_PLAYER_MEDIA_API_KEY },
    };

    stream_stats_t st = {
        .next_progress = STREAM_DEBUG_PROGRESS_BYTES,
        .started_us = esp_timer_get_time(),
    };

    ESP_LOGI(TAG, "streaming track %d (%s) from %s",
             track_id, (encoding && encoding[0]) ? encoding : "?", url);

    blob_store_open(track_id, encoding);

    http_stream_meta_t meta = { .content_length = -1 };
    esp_err_t err = http_get_stream(url, headers, 1, STREAM_DEBUG_TIMEOUT_MS,
                                    stream_chunk_cb, &st, &meta);

    blob_store_close();

    int64_t elapsed_us = esp_timer_get_time() - st.started_us;
    double kb_per_s = elapsed_us > 0
                          ? (st.total_bytes / 1024.0) / (elapsed_us / 1000000.0)
                          : 0.0;
    ESP_LOGI(TAG, "stream done: %s, %u bytes in %u chunks (min %u, max %u), %.1f kB/s",
             err == ESP_OK ? "ok" : esp_err_to_name(err),
             (unsigned)st.total_bytes, (unsigned)st.chunk_count,
             (unsigned)st.min_chunk, (unsigned)st.max_chunk, kb_per_s);

    if (meta.content_length >= 0) {
        if ((int64_t)st.total_bytes == meta.content_length) {
            ESP_LOGI(TAG, "byte count matches Content-Length (%d)", meta.content_length);
        } else {
            ESP_LOGW(TAG, "byte count %u != Content-Length %d",
                     (unsigned)st.total_bytes, meta.content_length);
        }
    }
}

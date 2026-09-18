#include "services/player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"

#include "data/device_state.h"
#include "net/http_client.h"
#include "net/media_client.h"
#include "services/audio_output.h"
#include "services/track_cache.h"

#define TAG "player"

#define PLAYER_TASK_STACK  16384
#define PLAYER_TASK_PRIO   5
#define STREAM_TIMEOUT_MS  20000
#define STREAM_MAX_RETRIES 3
#define DECODE_PCM_INITIAL 4096
#define FILE_CHUNK         4096

typedef enum {
    PLAY_NEXT = 0,  // finished normally, advance the queue
    PLAY_RESTART,   // a newer request replaced this one
    PLAY_STOP,
    PLAY_ERROR,
} play_result_t;

// ---- shared state ------------------------------------------------------------

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static player_track_t *s_tracks;
static size_t s_count;
static size_t s_index;

static volatile player_state_t s_state = PLAYER_IDLE;
static volatile int s_position_s;
static volatile uint8_t s_volume = 80;
static volatile bool s_stop_request;
static volatile bool s_restart_request;
static volatile bool s_paused;
static char s_error[320];

// ---- decoder -----------------------------------------------------------------

typedef struct {
    esp_audio_simple_dec_handle_t dec;
    uint8_t *pcm;
    size_t pcm_cap;
    int16_t *stereo;
    size_t stereo_cap;
    esp_audio_simple_dec_info_t info;
    uint64_t frames;
    bool decode_failed;
} decode_ctx_t;

static esp_audio_simple_dec_type_t dec_type_for(const char *encoding)
{
    if (!encoding) return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    if (strcasecmp(encoding, "wav") == 0)  return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    if (strcasecmp(encoding, "aac") == 0)  return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    if (strcasecmp(encoding, "m4a") == 0)  return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    if (strcasecmp(encoding, "flac") == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    if (strcasecmp(encoding, "ogg") == 0)  return ESP_AUDIO_SIMPLE_DEC_TYPE_OGG;
    return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
}

static bool decode_start(decode_ctx_t *ctx, const char *encoding)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->pcm = malloc(DECODE_PCM_INITIAL);
    if (!ctx->pcm) return false;
    ctx->pcm_cap = DECODE_PCM_INITIAL;

    esp_audio_simple_dec_cfg_t cfg = {
        .dec_type = dec_type_for(encoding),
        .dec_cfg = NULL,
        .cfg_size = 0,
        .use_frame_dec = false,
    };
    if (esp_audio_simple_dec_open(&cfg, &ctx->dec) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "failed to open decoder for '%s'", encoding ? encoding : "?");
        free(ctx->pcm);
        ctx->pcm = NULL;
        return false;
    }
    return true;
}

static void decode_stop(decode_ctx_t *ctx)
{
    if (ctx->dec) {
        esp_audio_simple_dec_close(ctx->dec);
    }
    free(ctx->pcm);
    free(ctx->stereo);
    memset(ctx, 0, sizeof(*ctx));
}

static bool decode_grow(decode_ctx_t *ctx, size_t needed)
{
    if (needed <= ctx->pcm_cap) return true;
    uint8_t *grown = realloc(ctx->pcm, needed);
    if (!grown) return false;
    ctx->pcm = grown;
    ctx->pcm_cap = needed;
    return true;
}

static bool output_pcm(decode_ctx_t *ctx, size_t bytes)
{
    uint8_t bps = ctx->info.bits_per_sample ? ctx->info.bits_per_sample : 16;
    uint8_t channels = ctx->info.channel ? ctx->info.channel : 2;
    uint32_t sample_rate = ctx->info.sample_rate ? ctx->info.sample_rate : 44100;

    if (bps != 16) {
        ESP_LOGW(TAG, "unsupported %u-bit PCM, skipping", bps);
        return true;
    }

    size_t samples = bytes / 2;
    if (channels >= 2) {
        audio_output_write((int16_t *)ctx->pcm, samples);
    } else {
        if (ctx->stereo_cap < samples * 2) {
            int16_t *grown = realloc(ctx->stereo, samples * 2 * sizeof(int16_t));
            if (!grown) return false;
            ctx->stereo = grown;
            ctx->stereo_cap = samples * 2;
        }
        const int16_t *src = (const int16_t *)ctx->pcm;
        for (size_t i = 0; i < samples; i++) {
            ctx->stereo[2 * i] = src[i];
            ctx->stereo[2 * i + 1] = src[i];
        }
        audio_output_write(ctx->stereo, samples * 2);
    }

    ctx->frames += samples / channels;
    s_position_s = (int)(ctx->frames / sample_rate);
    return true;
}

// Feeds encoded bytes through the decoder, pushing decoded PCM to the output.
static bool decode_feed(decode_ctx_t *ctx, const uint8_t *data, size_t len)
{
    esp_audio_simple_dec_raw_t raw = { .buffer = (uint8_t *)data, .len = len, .eos = false };

    while (raw.consumed < raw.len) {
        esp_audio_simple_dec_out_t out = { .buffer = ctx->pcm, .len = ctx->pcm_cap };
        esp_audio_err_t err = esp_audio_simple_dec_process(ctx->dec, &raw, &out);

        if (err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            if (!decode_grow(ctx, out.needed_size)) return false;
            continue;
        }
        if (err != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "decode error 0x%x", err);
            return false;
        }
        if (out.decoded_size) {
            esp_audio_simple_dec_get_info(ctx->dec, &ctx->info);
            if (!output_pcm(ctx, out.decoded_size)) return false;
        }
        if (raw.consumed == 0 && out.decoded_size == 0) {
            break; // no progress; wait for more input
        }
    }
    return true;
}

// ---- server streaming --------------------------------------------------------

typedef struct {
    decode_ctx_t dec;
    track_cache_writer_t *cache;
    size_t encoded_bytes;   // this attempt
    bool cache_failed;
    bool aborted;
} stream_ctx_t;

static bool stream_chunk_cb(void *vctx, const char *data, size_t len)
{
    stream_ctx_t *ctx = (stream_ctx_t *)vctx;

    while (s_paused) {
        if (s_stop_request || s_restart_request) {
            ctx->aborted = true;
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_stop_request || s_restart_request) {
        ctx->aborted = true;
        return false;
    }

    if (ctx->cache && track_cache_write(ctx->cache, data, len) != ESP_OK) {
        ctx->cache_failed = true;
    }
    ctx->encoded_bytes += len;

    if (!decode_feed(&ctx->dec, (const uint8_t *)data, len)) {
        ctx->aborted = true;
        return false;
    }
    return true;
}

static play_result_t play_server(const player_track_t *track)
{
    stream_ctx_t ctx = {0};
    if (!decode_start(&ctx.dec, track->encoding)) {
        snprintf(s_error, sizeof(s_error), "decoder open failed");
        return PLAY_ERROR;
    }

    char display_name[288];
    snprintf(display_name, sizeof(display_name), "%s - %s", track->artist, track->title);
    bool already_cached = false;
    if (track_cache_available()) {
        ctx.cache = track_cache_begin(display_name, track->encoding, &already_cached);
    }

    char url[256];
    if (media_stream_url(track->media_id, url, sizeof(url)) != ESP_OK) {
        if (ctx.cache) track_cache_abort(ctx.cache);
        decode_stop(&ctx.dec);
        snprintf(s_error, sizeof(s_error), "bad stream url");
        return PLAY_ERROR;
    }

    play_result_t result = PLAY_NEXT;
    size_t offset = 0;
    int retries = 0;

    while (!s_stop_request && !s_restart_request) {
        char range[32];
        http_header_t headers[2] = {
            { .key = "X-API-Key", .value = CONFIG_PLAYER_MEDIA_API_KEY },
        };
        size_t header_count = 1;
        if (offset > 0) {
            snprintf(range, sizeof(range), "bytes=%u-", (unsigned)offset);
            headers[header_count].key = "Range";
            headers[header_count].value = range;
            header_count++;
        }

        ctx.encoded_bytes = 0;
        ctx.aborted = false;
        esp_err_t err = http_get_stream(url, headers, header_count, STREAM_TIMEOUT_MS,
                                        stream_chunk_cb, &ctx, NULL);

        if (s_stop_request) { result = PLAY_STOP; break; }
        if (s_restart_request) { result = PLAY_RESTART; break; }
        if (ctx.dec.decode_failed) { result = PLAY_ERROR; break; }
        if (err == ESP_OK && !ctx.aborted) { result = PLAY_NEXT; break; } // EOF

        // Transport error: resume where we left off (the server supports Range).
        offset += ctx.encoded_bytes;
        if (++retries > STREAM_MAX_RETRIES) {
            snprintf(s_error, sizeof(s_error), "stream failed: %s", esp_err_to_name(err));
            ESP_LOGE(TAG, "%s", s_error);
            result = PLAY_ERROR;
            break;
        }
        ESP_LOGW(TAG, "stream interrupted (%s), resuming at byte %u",
                 esp_err_to_name(err), (unsigned)offset);
    }

    if (ctx.cache) {
        if (result == PLAY_NEXT && !ctx.cache_failed) {
            track_cache_end(ctx.cache);
        } else {
            track_cache_abort(ctx.cache);
        }
    }
    decode_stop(&ctx.dec);
    return result;
}

// ---- file playback -----------------------------------------------------------

static play_result_t play_file(const player_track_t *track)
{
    FILE *file = fopen(track->path, "rb");
    if (!file) {
        snprintf(s_error, sizeof(s_error), "cannot open %s", track->path);
        ESP_LOGE(TAG, "%s", s_error);
        return PLAY_ERROR;
    }

    decode_ctx_t dec;
    if (!decode_start(&dec, track->encoding)) {
        fclose(file);
        snprintf(s_error, sizeof(s_error), "decoder open failed");
        return PLAY_ERROR;
    }

    uint8_t *buf = malloc(FILE_CHUNK);
    if (!buf) {
        decode_stop(&dec);
        fclose(file);
        return PLAY_ERROR;
    }

    play_result_t result = PLAY_NEXT;
    for (;;) {
        while (s_paused) {
            if (s_stop_request || s_restart_request) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (s_stop_request) { result = PLAY_STOP; break; }
        if (s_restart_request) { result = PLAY_RESTART; break; }
        if (dec.decode_failed) { result = PLAY_ERROR; break; }

        size_t read_len = fread(buf, 1, FILE_CHUNK, file);
        if (read_len == 0) { result = PLAY_NEXT; break; } // EOF
        if (!decode_feed(&dec, buf, read_len)) { result = PLAY_ERROR; break; }
    }

    free(buf);
    decode_stop(&dec);
    fclose(file);
    return result;
}

// ---- task --------------------------------------------------------------------

static size_t queue_count_locked(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = s_count;
    xSemaphoreGive(s_lock);
    return count;
}

static play_result_t play_current(void)
{
    player_track_t track;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    track = s_tracks[s_index];
    xSemaphoreGive(s_lock);

    s_position_s = 0;
    s_error[0] = '\0';

    return track.source == PLAYER_TRACK_SERVER ? play_server(&track) : play_file(&track);
}

static void set_state(player_state_t state)
{
    s_state = state;
    // Unmute the speaker amplifier while playing, mute when idle/stopped so
    // the onboard DAC idle level does not hiss through the speaker.
    audio_output_amp_enable(state == PLAYER_PLAYING);
}

static void player_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (!s_stop_request) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            size_t index = s_index;
            size_t count = s_count;
            xSemaphoreGive(s_lock);

            if (count == 0 || index >= count) {
                set_state(PLAYER_IDLE);
                break;
            }

            set_state(PLAYER_PLAYING);
            play_result_t result = play_current();

            if (result == PLAY_STOP) { set_state(PLAYER_IDLE); break; }
            if (result == PLAY_ERROR) { set_state(PLAYER_ERROR); break; }
            if (result == PLAY_RESTART) {
                s_restart_request = false;
                continue;
            }

            // PLAY_NEXT: advance the queue
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_index++;
            bool done = s_index >= s_count;
            xSemaphoreGive(s_lock);
            if (done) { set_state(PLAYER_IDLE); break; }
        }

        s_stop_request = false;
        s_restart_request = false;
        s_paused = false;
        if (s_state != PLAYER_ERROR) {
            set_state(PLAYER_IDLE);
        }
    }
}

// ---- public API --------------------------------------------------------------

void player_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }

    esp_audio_simple_dec_register_default();

    s_volume = device_state_get_volume();
    audio_output_set_volume(s_volume);

    xTaskCreate(player_task, "player", PLAYER_TASK_STACK, NULL, PLAYER_TASK_PRIO, &s_task);
    ESP_LOGI(TAG, "player ready (volume %u%%)", s_volume);
}

esp_err_t player_play_queue(const player_track_t *tracks, size_t count, size_t start_index)
{
    if (!tracks || count == 0 || start_index >= count) return ESP_ERR_INVALID_ARG;

    player_track_t *copy = malloc(count * sizeof(*copy));
    if (!copy) return ESP_ERR_NO_MEM;
    memcpy(copy, tracks, count * sizeof(*copy));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    player_track_t *old = s_tracks;
    s_tracks = copy;
    s_count = count;
    s_index = start_index;
    xSemaphoreGive(s_lock);
    free(old);

    s_stop_request = false;
    s_restart_request = false;
    s_paused = false;
    s_position_s = 0;
    set_state(PLAYER_PLAYING);

    xTaskNotifyGive(s_task);
    return ESP_OK;
}

void player_toggle_pause(void)
{
    if (queue_count_locked() == 0) return;

    if (s_state == PLAYER_PLAYING) {
        s_paused = true;
        set_state(PLAYER_PAUSED);
    } else if (s_state == PLAYER_PAUSED) {
        s_paused = false;
        set_state(PLAYER_PLAYING);
    }
}

void player_next(void)
{
    if (queue_count_locked() == 0) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_index + 1 < s_count) {
        s_index++;
    } else {
        s_index = s_count; // signals "end of queue" to the task
    }
    xSemaphoreGive(s_lock);

    s_paused = false;
    s_restart_request = true;
    xTaskNotifyGive(s_task);
}

void player_prev(void)
{
    if (queue_count_locked() == 0) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_index > 0) {
        s_index--;
    }
    xSemaphoreGive(s_lock);

    s_paused = false;
    s_restart_request = true;
    xTaskNotifyGive(s_task);
}

void player_stop(void)
{
    s_stop_request = true;
    s_paused = false;
    xTaskNotifyGive(s_task);
}

void player_set_volume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_volume = percent;
    audio_output_set_volume(percent);
    device_state_set_volume(percent);
}

uint8_t player_get_volume(void)
{
    return s_volume;
}

void player_get_status(player_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_tracks && s_count > 0 && s_index < s_count) {
        player_track_t *track = &s_tracks[s_index];
        strncpy(out->title, track->title, sizeof(out->title) - 1);
        strncpy(out->artist, track->artist, sizeof(out->artist) - 1);
        out->duration_s = track->duration_s;
    }
    out->index = s_index;
    out->count = s_count;
    xSemaphoreGive(s_lock);

    out->state = s_state;
    out->position_s = s_position_s;
    out->volume = s_volume;
}

const char *player_error(void)
{
    return s_error;
}

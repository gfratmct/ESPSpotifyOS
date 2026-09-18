#include "services/audio_output.h"

#include <math.h>
#include <string.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "driver/gpio.h"
#include "sdkconfig.h"

#if defined(CONFIG_PLAYER_AUDIO_OUT_I2S)
#include "driver/i2s_std.h"
#endif
#if defined(CONFIG_PLAYER_AUDIO_OUT_DAC)
#include "driver/dac_continuous.h"
#endif

#define TAG "audio_output"

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS    2

// Amplifier enable polarity (E32R28T IO4: low level enable).
#ifdef CONFIG_PLAYER_AUDIO_EN_ACTIVE_LOW
#define AMP_ENABLE_LEVEL 0
#define AMP_MUTE_LEVEL   1
#else
#define AMP_ENABLE_LEVEL 1
#define AMP_MUTE_LEVEL   0
#endif

#if defined(CONFIG_PLAYER_AUDIO_OUT_DAC)
// Keep the DMA footprint small (TLS handshake memory is tight on this
// board): 4 x 1024 bytes buffers ~93 ms of audio at 44.1 kHz.
#define DAC_DESC_NUM      4
#define DAC_BUF_SIZE      1024
#define DAC_WRITE_TIMEOUT 1000
#endif

#if defined(CONFIG_PLAYER_TEST_TONE)
#define TONE_HZ        440
#define TONE_DURATION_MS 1000
#define TONE_AMPLITUDE (int16_t)(32767 * 0.3)
#define TONE_CHUNK_FRAMES 1024
#endif

typedef enum {
    OUTPUT_NONE = 0,
#if defined(CONFIG_PLAYER_AUDIO_OUT_I2S)
    OUTPUT_I2S,
#endif
#if defined(CONFIG_PLAYER_AUDIO_OUT_DAC)
    OUTPUT_DAC,
#endif
} output_backend_t;

static output_backend_t s_backend = OUTPUT_NONE;
static bool s_ready;
static uint8_t s_volume = 80;
static bool s_amp_ready;

#if defined(CONFIG_PLAYER_AUDIO_OUT_I2S)
static i2s_chan_handle_t s_tx;
#endif

#if defined(CONFIG_PLAYER_AUDIO_OUT_DAC)
static dac_continuous_handle_t s_dac;
static uint8_t *s_mono;
static size_t s_mono_cap;
#endif

static void apply_volume(int16_t *samples, size_t count)
{
    if (s_volume >= 100) {
        return;
    }
    if (s_volume == 0) {
        memset(samples, 0, count * sizeof(int16_t));
        return;
    }
    for (size_t i = 0; i < count; i++) {
        samples[i] = (int16_t)(((int32_t)samples[i] * s_volume) / 100);
    }
}

static void pace_silent(size_t sample_count)
{
    // No output: pace to real time so the UI progress is realistic.
    size_t frames = sample_count / AUDIO_CHANNELS;
    uint32_t ms = (uint32_t)((uint64_t)frames * 1000 / AUDIO_SAMPLE_RATE);
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

// ---- amplifier enable (E32R28T IO4) ------------------------------------------

static void amp_init(void)
{
#if CONFIG_PLAYER_AUDIO_EN_GPIO >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_PLAYER_AUDIO_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) == ESP_OK) {
        gpio_set_level(CONFIG_PLAYER_AUDIO_EN_GPIO, AMP_MUTE_LEVEL);
        s_amp_ready = true;
    } else {
        ESP_LOGW(TAG, "failed to configure amp enable GPIO %d", CONFIG_PLAYER_AUDIO_EN_GPIO);
    }
#endif
}

void audio_output_amp_enable(bool enable)
{
    if (!s_amp_ready) return;
    gpio_set_level(CONFIG_PLAYER_AUDIO_EN_GPIO, enable ? AMP_ENABLE_LEVEL : AMP_MUTE_LEVEL);
}

// ---- I2S backend (external DAC) -----------------------------------------------

#if defined(CONFIG_PLAYER_AUDIO_OUT_I2S)

static bool i2s_pins_configured(void)
{
    return CONFIG_PLAYER_AUX_BCK_GPIO >= 0 &&
           CONFIG_PLAYER_AUX_LRCK_GPIO >= 0 &&
           CONFIG_PLAYER_AUX_DIN_GPIO >= 0;
}

static esp_err_t i2s_output_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = CONFIG_PLAYER_AUX_SCK_GPIO,
            .bclk = CONFIG_PLAYER_AUX_BCK_GPIO,
            .ws = CONFIG_PLAYER_AUX_LRCK_GPIO,
            .dout = CONFIG_PLAYER_AUX_DIN_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { false, false, false },
        },
    };

    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }

    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }

    s_backend = OUTPUT_I2S;
    s_ready = true;
    ESP_LOGI(TAG, "I2S output ready (%d Hz, BCK=%d LRCK=%d DIN=%d SCK=%d)",
             AUDIO_SAMPLE_RATE, CONFIG_PLAYER_AUX_BCK_GPIO, CONFIG_PLAYER_AUX_LRCK_GPIO,
             CONFIG_PLAYER_AUX_DIN_GPIO, CONFIG_PLAYER_AUX_SCK_GPIO);
    return ESP_OK;
}

static esp_err_t i2s_output_write(const int16_t *samples, size_t sample_count)
{
    size_t bytes = sample_count * sizeof(int16_t);
    size_t written = 0;
    return i2s_channel_write(s_tx, samples, bytes, &written, portMAX_DELAY);
}

#endif // CONFIG_PLAYER_AUDIO_OUT_I2S

// ---- onboard DAC backend (E32R28T speaker, GPIO26) ----------------------------

#if defined(CONFIG_PLAYER_AUDIO_OUT_DAC)

static esp_err_t dac_output_init(void)
{
    // Channel 1 = GPIO26 on the ESP32; channel 0 (GPIO25) is used by the
    // touch controller on this board.
    dac_continuous_config_t cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH1,
        .desc_num = DAC_DESC_NUM,
        .buf_size = DAC_BUF_SIZE,
        .freq_hz = AUDIO_SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };

    esp_err_t err = dac_continuous_new_channels(&cfg, &s_dac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dac_continuous_new_channels failed: %s", esp_err_to_name(err));
        s_dac = NULL;
        return err;
    }

    err = dac_continuous_enable(s_dac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dac_continuous_enable failed: %s", esp_err_to_name(err));
        dac_continuous_del_channels(s_dac);
        s_dac = NULL;
        return err;
    }

    s_backend = OUTPUT_DAC;
    s_ready = true;
    ESP_LOGI(TAG, "onboard DAC output ready (%d Hz mono 8-bit on GPIO26, amp enable GPIO %d)",
             AUDIO_SAMPLE_RATE, CONFIG_PLAYER_AUDIO_EN_GPIO);
    ESP_LOGI(TAG, "heap after audio init: %u free, %u largest internal block",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return ESP_OK;
}

static esp_err_t dac_output_write(const int16_t *samples, size_t sample_count)
{
    size_t frames = sample_count / AUDIO_CHANNELS;
    if (frames == 0) return ESP_OK;

    if (s_mono_cap < frames) {
        uint8_t *grown = realloc(s_mono, frames);
        if (!grown) return ESP_ERR_NO_MEM;
        s_mono = grown;
        s_mono_cap = frames;
    }

    for (size_t i = 0; i < frames; i++) {
        int32_t left = samples[2 * i];
        int32_t right = samples[2 * i + 1];
        int32_t mono = (left + right) / 2;
        s_mono[i] = (uint8_t)((mono + 32768) >> 8);
    }

    size_t written = 0;
    while (written < frames) {
        size_t loaded = 0;
        esp_err_t err = dac_continuous_write(s_dac, s_mono + written, frames - written,
                                             &loaded, DAC_WRITE_TIMEOUT);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "dac_continuous_write failed: %s", esp_err_to_name(err));
            return err;
        }
        written += loaded;
    }
    return ESP_OK;
}

#endif // CONFIG_PLAYER_AUDIO_OUT_DAC

// ---- public API ---------------------------------------------------------------

esp_err_t audio_output_init(void)
{
    amp_init();

    esp_err_t err = ESP_ERR_INVALID_STATE;

#if defined(CONFIG_PLAYER_AUDIO_OUT_I2S)
    if (i2s_pins_configured()) {
        err = i2s_output_init();
    }
#endif

#if defined(CONFIG_PLAYER_AUDIO_OUT_DAC)
    err = dac_output_init();
#endif

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no audio output configured - playing silently");
    }

#if defined(CONFIG_PLAYER_TEST_TONE)
    audio_output_test_tone();
#endif

    return err;
}

bool audio_output_ready(void)
{
    return s_ready;
}

esp_err_t audio_output_write(int16_t *samples, size_t sample_count)
{
    if (!samples || sample_count == 0) return ESP_ERR_INVALID_ARG;

    apply_volume(samples, sample_count);

    if (!s_ready) {
        pace_silent(sample_count);
        return ESP_OK;
    }

    switch (s_backend) {
#if defined(CONFIG_PLAYER_AUDIO_OUT_I2S)
    case OUTPUT_I2S:
        return i2s_output_write(samples, sample_count);
#endif
#if defined(CONFIG_PLAYER_AUDIO_OUT_DAC)
    case OUTPUT_DAC:
        return dac_output_write(samples, sample_count);
#endif
    default:
        pace_silent(sample_count);
        return ESP_OK;
    }
}

#if defined(CONFIG_PLAYER_TEST_TONE)

void audio_output_test_tone(void)
{
    if (!s_ready) return;

    size_t total_frames = (size_t)AUDIO_SAMPLE_RATE * TONE_DURATION_MS / 1000;
    int16_t buf[TONE_CHUNK_FRAMES * AUDIO_CHANNELS];

    audio_output_amp_enable(true);
    ESP_LOGI(TAG, "test tone: %d Hz for %d ms", TONE_HZ, TONE_DURATION_MS);

    size_t frame = 0;
    while (frame < total_frames) {
        size_t n = total_frames - frame;
        if (n > TONE_CHUNK_FRAMES) n = TONE_CHUNK_FRAMES;
        for (size_t i = 0; i < n; i++) {
            double t = (double)(frame + i) / AUDIO_SAMPLE_RATE;
            int16_t v = (int16_t)(TONE_AMPLITUDE * sin(2.0 * M_PI * TONE_HZ * t));
            buf[2 * i] = v;
            buf[2 * i + 1] = v;
        }
        audio_output_write(buf, n * AUDIO_CHANNELS);
        frame += n;
    }

    // Let the DMA drain before muting so the tone tail is not clipped.
    vTaskDelay(pdMS_TO_TICKS(400));
    audio_output_amp_enable(false);
}

#else

void audio_output_test_tone(void)
{
}

#endif // CONFIG_PLAYER_TEST_TONE

void audio_output_set_volume(uint8_t percent)
{
    s_volume = percent > 100 ? 100 : percent;
}

uint8_t audio_output_get_volume(void)
{
    return s_volume;
}

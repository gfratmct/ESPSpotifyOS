#include "services/audio_output.h"

#include <string.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "driver/i2s_std.h"

#define TAG "audio_output"

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS    2

static i2s_chan_handle_t s_tx;
static bool s_ready;
static uint8_t s_volume = 80;

static bool output_configured(void)
{
    return CONFIG_PLAYER_AUX_BCK_GPIO >= 0 &&
           CONFIG_PLAYER_AUX_LRCK_GPIO >= 0 &&
           CONFIG_PLAYER_AUX_DIN_GPIO >= 0;
}

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

esp_err_t audio_output_init(void)
{
    if (!output_configured()) {
        ESP_LOGW(TAG, "no I2S output configured (set PLAYER_AUX_*_GPIO) — playing silently");
        return ESP_ERR_INVALID_STATE;
    }

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

    s_ready = true;
    ESP_LOGI(TAG, "I2S output ready (%d Hz, BCK=%d LRCK=%d DIN=%d SCK=%d)",
             AUDIO_SAMPLE_RATE, CONFIG_PLAYER_AUX_BCK_GPIO, CONFIG_PLAYER_AUX_LRCK_GPIO,
             CONFIG_PLAYER_AUX_DIN_GPIO, CONFIG_PLAYER_AUX_SCK_GPIO);
    return ESP_OK;
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
        // No DAC: pace to real time so the UI progress is realistic.
        size_t frames = sample_count / AUDIO_CHANNELS;
        uint32_t ms = (uint32_t)((uint64_t)frames * 1000 / AUDIO_SAMPLE_RATE);
        if (ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(ms));
        }
        return ESP_OK;
    }

    size_t bytes = sample_count * sizeof(int16_t);
    size_t written = 0;
    return i2s_channel_write(s_tx, samples, bytes, &written, portMAX_DELAY);
}

void audio_output_set_volume(uint8_t percent)
{
    s_volume = percent > 100 ? 100 : percent;
}

uint8_t audio_output_get_volume(void)
{
    return s_volume;
}

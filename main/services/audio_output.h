#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the configured audio output backend.
 *
 * The backend is selected via the PLAYER_AUDIO_OUTPUT Kconfig choice:
 * silent, external I2S DAC (PLAYER_AUX_* pins), or the onboard ESP32 DAC
 * (GPIO26, E32R28T speaker path). When no output can be configured this
 * returns ESP_ERR_INVALID_STATE but is non-fatal: playback still runs,
 * paced to real time, with no audio.
 */
esp_err_t audio_output_init(void);

// Whether a real audio output is actually configured/ready.
bool audio_output_ready(void);

/**
 * @brief Write interleaved 16-bit stereo PCM. Blocks while the output drains.
 *
 * The buffer is scaled in place by the current volume. Stereo is downmixed
 * to mono and reduced to 8 bits for the internal DAC backend.
 *
 * @param samples      interleaved left/right samples
 * @param sample_count number of int16 values (frames * 2)
 */
esp_err_t audio_output_write(int16_t *samples, size_t sample_count);

/**
 * @brief Enable or mute the speaker amplifier.
 *
 * Drives the PLAYER_AUDIO_EN_GPIO pin (IO4 on the E32R28T, active-low).
 * No-op when no enable GPIO is configured. The player unmutes on playback
 * and mutes again when idle.
 */
void audio_output_amp_enable(bool enable);

/**
 * @brief Play a short 440 Hz tone through the configured output.
 *
 * Used to verify the audio path (amp + speaker) without the streaming
 * pipeline. Enabled by CONFIG_PLAYER_TEST_TONE.
 */
void audio_output_test_tone(void);

// Software volume, 0-100.
void audio_output_set_volume(uint8_t percent);
uint8_t audio_output_get_volume(void);

#ifdef __cplusplus
}
#endif

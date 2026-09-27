#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the amplifier enable pin (kept muted). Cheap and safe to
 *        call at boot.
 *
 * The actual output backend (ESP32 DAC or external I2S DAC, including its DMA
 * buffers) is opened lazily by audio_output_open() on first playback, so the
 * boot-time heap stays free for the Wi-Fi/TLS stack. Playback works silently
 * (paced to real time) while the backend is not open.
 */
esp_err_t audio_output_init(void);

/**
 * @brief Open the configured output backend (idempotent).
 *
 * Allocates the DAC/I2S DMA buffers. Call before the first audio_output_write()
 * for real sound; safe to call repeatedly.
 */
esp_err_t audio_output_open(void);

/**
 * @brief Release the output backend and its DMA buffers (idempotent).
 */
void audio_output_close(void);

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

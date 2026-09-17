#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the I2S output from the PLAYER_AUX_* Kconfig pins.
 *
 * When no pins are configured (all -1) this returns ESP_ERR_INVALID_STATE but
 * is non-fatal: playback still runs, paced to real time, with no audio.
 */
esp_err_t audio_output_init(void);

// Whether an I2S output is actually configured/ready.
bool audio_output_ready(void);

/**
 * @brief Write interleaved 16-bit stereo PCM. Blocks while the DMA drains.
 *
 * The buffer is scaled in place by the current volume.
 *
 * @param samples      interleaved left/right samples
 * @param sample_count number of int16 values (frames * 2)
 */
esp_err_t audio_output_write(int16_t *samples, size_t sample_count);

// Software volume, 0-100.
void audio_output_set_volume(uint8_t percent);
uint8_t audio_output_get_volume(void);

#ifdef __cplusplus
}
#endif

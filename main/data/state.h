#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all persistent state (auth + device).
 *
 * Loads both namespaces into RAM and, on the first boot after an upgrade,
 * migrates the legacy combined `app_state` blob into the split namespaces.
 * Missing state falls back to defaults.
 */
esp_err_t state_init(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the TFT tab UI and start the poll timer.
 *        Call after display_init(), lvgl_init(), and buttons_init().
 */
esp_err_t ui_init(void);

#ifdef __cplusplus
}
#endif

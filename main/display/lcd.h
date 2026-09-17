#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lcd_init(void);
esp_err_t lcd_start_lvgl(void);
void lcd_backlight_init(void);
void lcd_backlight_set(uint8_t percent);

#ifdef __cplusplus
}
#endif

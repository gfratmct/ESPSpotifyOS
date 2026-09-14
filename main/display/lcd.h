#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lcd_init(void);
esp_err_t lcd_start_lvgl(void);
lv_display_t *lcd_get_display(void);
esp_lcd_touch_handle_t lcd_get_touch(void);
void lcd_backlight_init(void);
void lcd_backlight_set(uint8_t percent);

#ifdef __cplusplus
}
#endif

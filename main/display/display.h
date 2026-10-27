#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "lvgl.h"

// HS20HS072RX: ST7789T3, portrait 240x320, 4-wire SPI.
#define DISPLAY_W 240
#define DISPLAY_H 320

// Partial redraw: two stripes of 20 lines (RGB565) stay in internal DMA RAM.
// A full frame is 153 KB, which does not fit alongside Wi-Fi and audio.
#define DISPLAY_DRAW_BUF_LINES 20

#define DISPLAY_TICK_PERIOD_MS 5
#define DISPLAY_LV_TASK_MIN_DELAY_MS 5
#define DISPLAY_LV_TASK_MAX_DELAY_MS 20
#define DISPLAY_LV_TASK_STACK_SIZE (12 * 1024)
#define DISPLAY_LV_TASK_PRIORITY 5

// SPI3. SPI2 is the SD card.
#define LCD_SPI_HOST SPI3_HOST
#define LCD_PCLK_HZ (20 * 1000 * 1000)

#define LCD_PIN_SCLK GPIO_NUM_8
#define LCD_PIN_MOSI GPIO_NUM_6
#define LCD_PIN_CS   GPIO_NUM_10
#define LCD_PIN_DC   GPIO_NUM_9   // module "RS"
#define LCD_PIN_RST  GPIO_NUM_5
#define LCD_PIN_BL   GPIO_NUM_7   // backlight PWM, active high

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_init(void);
esp_err_t lvgl_init(void);
lv_display_t *get_disp(void);

/** Take/release the LVGL task lock (required when mutating UI outside lv_port_task). */
void display_lv_lock(void);
void display_lv_unlock(void);

#ifdef __cplusplus
}
#endif

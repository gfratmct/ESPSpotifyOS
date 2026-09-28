#pragma once

#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>

#include "display/display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "lvgl.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"

#define DISPLAY_W 128
#define DISPLAY_H 64
#define DISPLAY_PAGES (DISPLAY_H / 8)
#define DISPLAY_PALETTE_SIZE 8
#define DISPLAY_TICK_PERIOD_MS 5
#define DISPLAY_LV_TASK_MIN_DELAY_MS 5
#define DISPLAY_LV_TASK_MAX_DELAY_MS 20
#define DISPLAY_LV_TASK_STACK_SIZE 4096
#define DISPLAY_LV_TASK_PRIORITY 5

#define SSD1306_CMD_SET_CONTRAST    0x81
#define SSD1306_CMD_RAM_CONTENTS    0xA4    /* resume following the RAM */
#define SSD1306_CMD_ALL_PIXELS_ON   0xA5    /* light every segment */

#define I2C_PIN_SDA GPIO_NUM_6
#define I2C_PIN_SCL GPIO_NUM_8
#define I2C_LCD_ADDR 0x3C
#define I2C_FREQ_HZ (400 * 1000) // based on the LCD specs 400 kHz is the max supported 
#define I2C_LCD_RESET -1 // Not used, as the LCD does not have a reset pin connected
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_init(void);
esp_err_t lvgl_init(void);
lv_display_t *get_disp(void);

#ifdef __cplusplus
}
#endif
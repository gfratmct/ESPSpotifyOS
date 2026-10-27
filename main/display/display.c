#include "display/display.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>

static const char *TAG = "display";

static esp_lcd_panel_handle_t _panel_handle = NULL;
static esp_lcd_panel_io_handle_t _io_handle = NULL;
static lv_display_t *_display;
static _lock_t lv_task_lock;

// Datasheet HS20HS072RX (ST7789T3) porch, power, and gamma.
// Portrait MADCTL (0x36 = 0x00) and RGB565 COLMOD are set by the panel driver.
// The datasheet sequence does not send INVON, so colors stay uninverted.
static const uint8_t s_porch[] = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
static const uint8_t s_gate[] = { 0x35 };
static const uint8_t s_vcom[] = { 0x20 };
static const uint8_t s_lcm[] = { 0x2C };
static const uint8_t s_vdv_vrh_en[] = { 0x01 };
static const uint8_t s_vrh[] = { 0x0B };
static const uint8_t s_vdv[] = { 0x20 };
static const uint8_t s_frame[] = { 0x0F };
static const uint8_t s_power[] = { 0xA4, 0xA1 };
static const uint8_t s_gamma_p[] = {
    0xD0, 0x03, 0x09, 0x0E, 0x11, 0x3D, 0x47,
    0x55, 0x53, 0x1A, 0x16, 0x14, 0x1F, 0x22,
};
static const uint8_t s_gamma_n[] = {
    0xD0, 0x02, 0x08, 0x0D, 0x12, 0x2C, 0x43,
    0x55, 0x53, 0x1E, 0x1B, 0x19, 0x20, 0x22,
};

static esp_err_t panel_vendor_init(esp_lcd_panel_io_handle_t io)
{
    // Each call is one ST7789 command plus its parameter bytes.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xB2, s_porch, sizeof(s_porch)),
                        TAG, "porch");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xB7, s_gate, sizeof(s_gate)),
                        TAG, "gate");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xBB, s_vcom, sizeof(s_vcom)),
                        TAG, "vcom");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC0, s_lcm, sizeof(s_lcm)),
                        TAG, "lcm");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC2, s_vdv_vrh_en, sizeof(s_vdv_vrh_en)),
                        TAG, "vdv/vrh en");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC3, s_vrh, sizeof(s_vrh)),
                        TAG, "vrh");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC4, s_vdv, sizeof(s_vdv)),
                        TAG, "vdv");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xC6, s_frame, sizeof(s_frame)),
                        TAG, "frame");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xD0, s_power, sizeof(s_power)),
                        TAG, "power");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xE0, s_gamma_p, sizeof(s_gamma_p)),
                        TAG, "gamma+");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xE1, s_gamma_n, sizeof(s_gamma_n)),
                        TAG, "gamma-");
    return ESP_OK;
}

// Backlight is four white LEDs. IO7 is PWM, active high: duty 255 is full on.
// Held off until the panel has been initialized so reset garbage stays dark.
static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");

    const ledc_channel_config_t channel = {
        .gpio_num = LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel);
}

static void backlight_on(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 255);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

lv_display_t *get_disp(void)
{
    return _display;
}

void display_lv_lock(void)
{
    _lock_acquire(&lv_task_lock);
}

void display_lv_unlock(void)
{
    _lock_release(&lv_task_lock);
}

esp_err_t display_init(void)
{
    if (_panel_handle) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    // Write-only SPI: the module has SDA (MOSI) and SCL, no MISO.
    const spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_W * DISPLAY_DRAW_BUF_LINES * (int)sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &io_handle),
                        TAG, "panel io");

    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        // LVGL RGB565 is little-endian in memory. RAMCTRL bit 3 tells the
        // ST7789T3 to accept that order, so the flush path can DMA pixels as-is.
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle),
                        TAG, "st7789");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "init");
    // Native 240x320 glass: no column/row offset (those belong to 240x240 panels).
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel_handle, 0, 0), TAG, "gap");
    // First-boot orientation. Flip these if the module is mounted the other way.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_handle, false, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle, false), TAG, "invert");
    ESP_RETURN_ON_ERROR(panel_vendor_init(io_handle), TAG, "vendor init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "display on");

    backlight_on();

    _panel_handle = panel_handle;
    _io_handle = io_handle;
    ESP_LOGI(TAG, "ST7789 ready (%dx%d, SPI%d, %d Hz)",
             DISPLAY_W, DISPLAY_H, LCD_SPI_HOST + 1, LCD_PCLK_HZ);
    return ESP_OK;
}

static void lv_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    // draw_bitmap's end coordinates are exclusive. DMA completion calls
    // lv_display_flush_ready from the panel IO callback.
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
}

static bool lv_notify_flush_ready(esp_lcd_panel_io_handle_t io_handle,
                                  esp_lcd_panel_io_event_data_t *edata,
                                  void *user_data)
{
    (void)io_handle;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)user_data);
    return false;
}

static void increase_lv_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(DISPLAY_TICK_PERIOD_MS);
}

static void lv_port_task(void *arg)
{
    (void)arg;
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lv_task_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lv_task_lock);
        time_till_next_ms = MAX(time_till_next_ms, DISPLAY_LV_TASK_MIN_DELAY_MS);
        time_till_next_ms = MIN(time_till_next_ms, DISPLAY_LV_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

esp_err_t lvgl_init(void)
{
    if (_display) {
        return ESP_OK;
    }

    lv_init();

    if (_panel_handle == NULL || _io_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_display_t *disp = lv_display_create(DISPLAY_W, DISPLAY_H);
    lv_display_set_user_data(disp, _panel_handle);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    const size_t buffer_size = (size_t)DISPLAY_W * DISPLAY_DRAW_BUF_LINES * sizeof(uint16_t);
    void *buf1 = spi_bus_dma_memory_alloc(LCD_SPI_HOST, buffer_size, 0);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_SPI_HOST, buffer_size, 0);
    if (!buf1 || !buf2) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(disp, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lv_flush_cb);

#if LV_FONT_MONTSERRAT_16
    const lv_font_t *font = &lv_font_montserrat_16;
#else
    const lv_font_t *font = LV_FONT_DEFAULT;
#endif
    // Dark default theme: white text on black, green as the primary accent.
    lv_display_set_theme(disp, lv_theme_default_init(disp,
                                                     lv_palette_main(LV_PALETTE_GREEN),
                                                     lv_palette_main(LV_PALETTE_GREY),
                                                     true, font));
    lv_theme_apply(lv_display_get_screen_active(disp));

    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = lv_notify_flush_ready,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(_io_handle, &callbacks, disp),
                        TAG, "flush callback");

    const esp_timer_create_args_t lv_tick_timer_args = {
        .callback = increase_lv_tick,
        .name = "lv_tick_timer",
    };
    esp_timer_handle_t lv_tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&lv_tick_timer_args, &lv_tick_timer), TAG, "tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(lv_tick_timer, DISPLAY_TICK_PERIOD_MS * 1000),
                        TAG, "tick start");

    if (xTaskCreate(lv_port_task, "lv_port_task", DISPLAY_LV_TASK_STACK_SIZE,
                    NULL, DISPLAY_LV_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    _display = disp;
    ESP_LOGI(TAG, "LVGL RGB565 ready (%d-line partial buffers)", DISPLAY_DRAW_BUF_LINES);
    return ESP_OK;
}

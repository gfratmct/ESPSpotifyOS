#include "display/display.h"

#include <string.h>

#include "esp_heap_caps.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t _panel_handle = NULL;
static esp_lcd_panel_io_handle_t _io_handle = NULL;
static lv_display_t *_display;
static uint8_t oled_buffer[DISPLAY_W * DISPLAY_H / 8];
static _lock_t lv_task_lock;

static esp_lcd_panel_handle_t get_panel_handle(void)
{
    return _panel_handle;
}

static esp_lcd_panel_io_handle_t get_io_handle(void)
{
    return _io_handle;
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

    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_BUS_PORT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .sda_io_num = I2C_PIN_SDA,
        .scl_io_num = I2C_PIN_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = I2C_LCD_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
        .transaction_timeout_ms = 1000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_ssd1306_config_t panel_ssd1306_config = {
        .height = DISPLAY_H,
        .contrast = 0xCF,
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = I2C_LCD_RESET,
        .vendor_config = &panel_ssd1306_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    _panel_handle = panel_handle;
    _io_handle = io_handle;

    ESP_LOGI(TAG, "SSD1306 ready (%dx%d @ 0x%02X)", DISPLAY_W, DISPLAY_H, I2C_LCD_ADDR);
    return ESP_OK;
}

static void lv_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);

    // LVGL I1 reserves 2 x lv_color32_t as a palette at the front of the buffer.
    px_map += DISPLAY_PALETTE_SIZE;

    const int32_t stride = lv_draw_buf_width_to_stride(DISPLAY_W, LV_COLOR_FORMAT_I1);
    const int x1 = area->x1;
    const int x2 = area->x2;
    const int y1 = area->y1;
    const int y2 = area->y2;

    // Rebuild only the touched pages so partial updates stay coherent.
    const int page0 = y1 >> 3;
    const int page1 = y2 >> 3;
    for (int page = page0; page <= page1; page++) {
        memset(oled_buffer + (size_t)page * DISPLAY_W, 0, DISPLAY_W);
    }

    for (int y = y1; y <= y2; y++) {
        const uint8_t *row = px_map + (size_t)stride * (size_t)y;
        for (int x = x1; x <= x2; x++) {
            // LVGL I1: MSB first within each byte (pixel 0 = bit 7).
            const bool on = (row[x >> 3] & (uint8_t)(1u << (7 - (x & 7)))) != 0;
            uint8_t *dst = oled_buffer + (size_t)DISPLAY_W * (size_t)(y >> 3) + (size_t)x;
            // Normal SSD1306 polarity: 1 = pixel lit. (Espressif's example inverts;
            // that produces a white wash with black speckles for dark-theme UIs.)
            if (on) {
                *dst |= (uint8_t)(1u << (y & 7));
            } else {
                *dst &= (uint8_t)~(1u << (y & 7));
            }
        }
    }

    // FULL render mode always flushes the whole panel starting at (0,0), so
    // the GDDRAM stream begins at oled_buffer[0].
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, oled_buffer);
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

    esp_lcd_panel_handle_t panel_handle = get_panel_handle();
    esp_lcd_panel_io_handle_t io_handle = get_io_handle();
    if (panel_handle == NULL || io_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_display_t *disp = lv_display_create(DISPLAY_W, DISPLAY_H);
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);

    size_t buffer_size = (size_t)DISPLAY_W * DISPLAY_H / 8 + DISPLAY_PALETTE_SIZE;
    void *buffer = heap_caps_calloc(1, buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buffer) {
        return ESP_ERR_NO_MEM;
    }

    lv_display_set_buffers(disp, buffer, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, lv_flush_cb);

    // Dark mono theme = white text on black OLED background.
#if LV_FONT_UNSCII_8
    const lv_font_t *font = &lv_font_unscii_8;
#elif LV_FONT_MONTSERRAT_12
    const lv_font_t *font = &lv_font_montserrat_12;
#elif LV_FONT_MONTSERRAT_14
    const lv_font_t *font = &lv_font_montserrat_14;
#elif LV_FONT_MONTSERRAT_10
    const lv_font_t *font = &lv_font_montserrat_10;
#else
    const lv_font_t *font = LV_FONT_DEFAULT;
#endif
    lv_display_set_theme(disp, lv_theme_mono_init(disp, true /* dark_bg */, font));
    // lv_display_create() already built the first screen with the light default
    // theme; without re-applying, it keeps a white background.
    lv_theme_apply(lv_display_get_screen_active(disp));

    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = lv_notify_flush_ready,
    };
    esp_lcd_panel_io_register_event_callbacks(io_handle, &callbacks, disp);

    const esp_timer_create_args_t lv_tick_timer_args = {
        .callback = increase_lv_tick,
        .name = "lv_tick_timer",
    };
    esp_timer_handle_t lv_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lv_tick_timer_args, &lv_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lv_tick_timer, DISPLAY_TICK_PERIOD_MS * 1000));

    BaseType_t ok = xTaskCreate(lv_port_task, "lv_port_task", DISPLAY_LV_TASK_STACK_SIZE,
                                NULL, DISPLAY_LV_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    _display = disp;
    ESP_LOGI(TAG, "LVGL I1 + mono theme ready");
    return ESP_OK;
}

#include "display/display.h"

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

lv_display_t *get_display(void)
{
    return _display;
}

esp_err_t display_init(void)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,
        .glitch_ignore_cnt = 4,
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
        .control_phase_bytes = 1, // refer to LCD spec
        .dc_bit_offset = 6,       // refer to LCD spec
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_ssd1306_config_t panel_ssd1306_config = {
        .height = DISPLAY_H,
        .contrast = 128
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1, // refer to LCD spec
        .reset_gpio_num = I2C_LCD_RESET,
        .vendor_config = &panel_ssd1306_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, true));

    // store the panel handle in the static variable
    _panel_handle = panel_handle;
    _io_handle = io_handle;

    return ESP_OK;
}

static void lv_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    px_map += DISPLAY_PALETTE_SIZE;
    
    uint16_t h_res = lv_display_get_physical_horizontal_resolution(disp);
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            bool chroma_color = (px_map[(h_res >> 3) * y  + (x >> 3)] & 1 << (7 - x % 8));

            /* Write to the buffer as required for the display.
            * It writes only 1-bit for monochrome displays mapped vertically.*/
            uint8_t *buf = oled_buffer + h_res * (y >> 3) + (x);
            if (chroma_color) {
                (*buf) &= ~(1 << (y % 8));
            } else {
                (*buf) |= (1 << (y % 8));
            }
        }
    }

    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, oled_buffer);
}

static bool lv_notify_flush_ready(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_io_event_data_t *edata, void *user_data)
{
    lv_display_t *disp = (lv_display_t *)user_data;
    lv_display_flush_ready(disp);
    return false;
}

static void increase_lv_tick(lv_display_t *disp)
{
    lv_tick_inc(DISPLAY_TICK_PERIOD_MS);
}


static void lv_port_task(void *arg)
{
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lv_task_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lv_task_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, DISPLAY_LV_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, DISPLAY_LV_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

esp_err_t lvgl_init(void)
{
    lv_init();

    esp_lcd_panel_handle_t panel_handle = get_panel_handle();
    esp_lcd_panel_io_handle_t io_handle = get_io_handle();

    if (panel_handle == NULL) return ESP_ERR_INVALID_STATE;
    lv_display_t *disp = lv_display_create(DISPLAY_W, DISPLAY_H);
    
    lv_display_set_user_data(disp, panel_handle);
    
    void *buffer = NULL;
    size_t buffer_size = DISPLAY_W * DISPLAY_H / 8 + DISPLAY_PALETTE_SIZE;
    buffer = heap_caps_calloc(1, buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(buffer);
    
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(disp, buffer, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, lv_flush_cb);

    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = lv_notify_flush_ready,
    };

    esp_lcd_panel_io_register_event_callbacks(io_handle, &callbacks, disp);

    const esp_timer_create_args_t lv_tick_timer_args = {
        .callback = (esp_timer_cb_t)increase_lv_tick,
        .name = "lv_tick_timer",
    };
    esp_timer_handle_t lv_tick_timer = NULL;

    ESP_ERROR_CHECK(esp_timer_create(&lv_tick_timer_args, &lv_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lv_tick_timer, DISPLAY_TICK_PERIOD_MS * 1000));

    // create task
    xTaskCreate(lv_port_task, "lv_port_task", DISPLAY_LV_TASK_STACK_SIZE, NULL, DISPLAY_LV_TASK_PRIORITY, NULL);

    // a quick test to ensure the LVGL task is running
    _lock_acquire(&lv_task_lock);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    /* Size of the screen (if you use rotation 90 or 270, please use lv_display_get_vertical_resolution) */
    lv_obj_set_width(label, lv_display_get_horizontal_resolution(disp));
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    _lock_release(&lv_task_lock);

    // if all is ok save the display instance
    _display = disp;
    
    return ESP_OK;
}
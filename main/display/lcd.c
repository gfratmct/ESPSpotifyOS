#include "lcd.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define LCD_SPI_HOST SPI2_HOST
#define LCD_SPI_MOSI_GPIO 13
#define LCD_SPI_MISO_GPIO 12
#define LCD_SPI_SCLK_GPIO 14
#define LCD_SPI_LCD_CS_GPIO 15
#define LCD_SPI_LCD_DC_GPIO 2
#define LCD_SPI_LCD_RST_GPIO GPIO_NUM_NC
#define LCD_BACKLIGHT_GPIO 21

#define TOUCH_SPI_HOST SPI3_HOST
#define TOUCH_SPI_MOSI_GPIO 32
#define TOUCH_SPI_MISO_GPIO 39
#define TOUCH_SPI_SCLK_GPIO 25
#define TOUCH_SPI_CS_GPIO 33
#define TOUCH_IRQ_GPIO 36

#define RGB_RED_GPIO 22
#define RGB_GREEN_GPIO 16
#define RGB_BLUE_GPIO 17
#define LCD_DRAW_BUFFER_LINES 40

static const char *TAG = "lcd";
static lv_display_t *s_display;
static esp_lcd_touch_handle_t s_touch;
static esp_lcd_panel_handle_t s_panel;

static bool lcd_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_io_event_data_t *event_data,
                            void *user_ctx) {
    (void)panel_io;
    (void)event_data;
    lv_display_flush_ready(user_ctx != NULL ? (lv_display_t *)user_ctx : s_display);
    return false;
}

static void lcd_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                                               area->x2 + 1, area->y2 + 1,
                                               px_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD flush failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(display);
    }
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    static bool was_pressed = false;
    static uint16_t last_x = 0;
    static uint16_t last_y = 0;

    uint8_t count = 0;
    esp_lcd_touch_point_data_t touch_data[1];

    esp_err_t err = esp_lcd_touch_read_data(s_touch);
    if (err == ESP_OK) {
        err = esp_lcd_touch_get_data(s_touch, touch_data, &count, 1);
    }

    if (err == ESP_OK && count > 0) {
        last_x = touch_data->x;
        last_y = touch_data->y;
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_PRESSED;
        if (!was_pressed) {
            ESP_LOGI(TAG, "touch pressed at (%u, %u)", last_x, last_y);
            was_pressed = true;
        }
    } else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED;
        if (was_pressed) {
            ESP_LOGI(TAG, "touch released");
            was_pressed = false;
        }
    }
}

static void lvgl_tick_callback(void *arg) {
    (void)arg;
    lv_tick_inc(2);
}

static void lvgl_task(void *arg) {
    (void)arg;
    while (true) {
        uint32_t delay_ms = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(delay_ms < 5 ? 5 : delay_ms));
    }
}

esp_err_t lcd_init(void)
{
    spi_bus_config_t lcd_bus_config = {
        .sclk_io_num = LCD_SPI_SCLK_GPIO,
        .mosi_io_num = LCD_SPI_MOSI_GPIO,
        .miso_io_num = LCD_SPI_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_LCD_HRES * LCD_DRAW_BUFFER_LINES * 3,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &lcd_bus_config, SPI_DMA_CH_AUTO),
                        TAG, "failed to initialize LCD SPI bus");

    spi_bus_config_t touch_bus_config = {
        .sclk_io_num = TOUCH_SPI_SCLK_GPIO,
        .mosi_io_num = TOUCH_SPI_MOSI_GPIO,
        .miso_io_num = TOUCH_SPI_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(TOUCH_SPI_HOST, &touch_bus_config, SPI_DMA_CH_AUTO),
                        TAG, "failed to initialize touch SPI bus");

    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << RGB_RED_GPIO) |
                        (1ULL << RGB_GREEN_GPIO) |
                        (1ULL << RGB_BLUE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), TAG, "failed to configure RGB GPIOs");
    gpio_set_level(RGB_RED_GPIO, 1);
    gpio_set_level(RGB_GREEN_GPIO, 1);
    gpio_set_level(RGB_BLUE_GPIO, 1);

    esp_lcd_panel_io_spi_config_t lcd_io_config = ILI9341_PANEL_IO_SPI_CONFIG(
        LCD_SPI_LCD_CS_GPIO, LCD_SPI_LCD_DC_GPIO, lcd_flush_ready, NULL);
    esp_lcd_panel_io_handle_t lcd_io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &lcd_io_config, &lcd_io),
                        TAG, "failed to create LCD panel IO");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_SPI_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 18,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(lcd_io, &panel_config, &s_panel),
                        TAG, "failed to create ILI9341 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "failed to reset LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "failed to initialize LCD panel");
    // The panel is natively 240x320 (portrait)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, false), TAG, "failed to swap LCD panel axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "failed to mirror LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "failed to turn on LCD panel");

    esp_lcd_panel_io_spi_config_t touch_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(
        TOUCH_SPI_CS_GPIO);
    esp_lcd_panel_io_handle_t touch_io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(TOUCH_SPI_HOST, &touch_io_config, &touch_io),
                        TAG, "failed to create touch IO");


    // touch screen config
    esp_lcd_touch_config_t touch_config = {
        .x_max = CONFIG_LCD_HRES,
        .y_max = CONFIG_LCD_VRES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = TOUCH_IRQ_GPIO,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 1, .mirror_y = 0 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(touch_io, &touch_config, &s_touch),
                        TAG, "failed to create XPT2046 touch controller");

    lv_init();
    s_display = lv_display_create(CONFIG_LCD_HRES, CONFIG_LCD_VRES);
    if (s_display == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_flush_cb(s_display, lcd_flush);

    size_t buffer_size = CONFIG_LCD_HRES * LCD_DRAW_BUFFER_LINES * 3;
    void *buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(s_display, buffer, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);
    lv_indev_set_display(indev, s_display);

    esp_timer_handle_t tick_timer;
    const esp_timer_create_args_t tick_args = { .callback = lvgl_tick_callback, .name = "lvgl_tick" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "failed to create LVGL tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, 2000), TAG, "failed to start LVGL tick timer");

    ESP_LOGI(TAG, "ILI9341 LCD and XPT2046 touch initialized");
    return ESP_OK;
}

/* Start the LVGL render loop. Must be called only after all widgets have been
 * created (LVGL is not thread-safe: rendering must not race with widget
 * creation from the main task). */
esp_err_t lcd_start_lvgl(void)
{
    if (xTaskCreate(lvgl_task, "lvgl", 12288, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void lcd_backlight_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num = LCD_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&channel);
}

void lcd_backlight_set(uint8_t percent) {
    uint32_t duty = (percent * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "ui.h"

#include "display/lcd.h"
#include "connectivity/wifi.h"
#include "server/http.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "initializing display");
    ESP_ERROR_CHECK(lcd_init());
    lcd_backlight_init();
    lcd_backlight_set(20);

    // setup ui
    ESP_LOGI(TAG, "initializing ui");
    ui_init();

    // lets connect to the wifi
    ESP_ERROR_CHECK(wifi_start());
    if (!wifi_wait_connected(15000) || !wifi_is_connected()) {
        ESP_LOGE(TAG, "Wifi connection failed! Webserver will NOT start.");
    } else {
        ESP_LOGI(TAG, "Wifi connected successfully!");
        // and then setup and start webserver
        webserver_t *webserver = webserver_create(8080);
        if (webserver_start(webserver) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start webserver");
        }
    }

    /* Start rendering only after the whole UI tree exists. */
    ESP_ERROR_CHECK(lcd_start_lvgl());

    ESP_LOGI(TAG, "ESPSpotifyOS ready");
}
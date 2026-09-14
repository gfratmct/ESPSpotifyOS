#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "lcd.h"
#include "nvs_flash.h"
#include "ui.h"
#include "wifi.h"
#include "http.h"

static const char *TAG = "main";

// routes
static esp_err_t handle_submit(httpd_req_t *req)
{
    // write a simple hello world for now
    const char *resp_str = "Hello, world!";
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, resp_str, strlen(resp_str));
}

void turn_on_webserver(void) {
    webserver_t *webserver = webserver_create(8080);
    ESP_LOGI(TAG, "Webserver instance created");

    if (!webserver) {
        ESP_LOGE(TAG, "Webserver instance not found");
        return;
    }

    if (webserver_start(webserver) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver");
        return;
    }

    route_get_path(webserver, "/submit", handle_submit);

    webserver_t *ws_instance = webserver_get_instance();
    if (!ws_instance) {
        ESP_LOGE(TAG, "Failed to get webserver instance");
        return;
    }
}

void turn_off_webserver(void) {
    webserver_t *ws_instance = webserver_get_instance();
    if (ws_instance) {
        webserver_stop(ws_instance);
        webserver_destroy(ws_instance);
    }
}

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
    lcd_backlight_set(60);

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
        turn_on_webserver();
    }

    /* Start rendering only after the whole UI tree exists. */
    ESP_ERROR_CHECK(lcd_start_lvgl());

    ESP_LOGI(TAG, "ESPSpotifyOS ready");
}
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui.h"

#include "connectivity/wifi.h"
#include "core/time_sync.h"
#include "data/auth_state.h"
#include "data/device_state.h"
#include "data/state.h"
#include "display/lcd.h"
#include "display/library_screen.h"
#include "display/setup_screen.h"
#include "display/ui_state.h"
#include "net/spotify_auth.h"
#include "server/webserver.h"
#include "storage/sd_storage.h"
#include "utils/storage.h"

static const char *TAG = "main";
#define APP_WEBSERVER_PORT CONFIG_PLAYER_WEBSERVER_PORT

void app_main(void)
{
    ESP_ERROR_CHECK(storage_init());
    state_init();

    auth_state_t *auth = auth_state_get();
    device_state_t *device = device_state_get();
    ESP_LOGI(TAG, "State: logged_in=%d, current_screen=%d, wifi_ssid=%s",
             auth->is_logged_in, device->current_screen, device->wifi_ssid);

    ESP_LOGI(TAG, "initializing display");
    ESP_ERROR_CHECK(lcd_init());
    lcd_backlight_init();
    lcd_backlight_set(20);

    // SD shares the LCD SPI bus, so mount it after lcd_init(). A missing card
    // is not fatal: the offline tab simply reports storage as unavailable.
    sd_storage_mount();

    ESP_LOGI(TAG, "initializing ui");
    ui_init();
    library_screen_init();

    // Connect to Wi-Fi. On failure, fall back to a provisioning access point
    // so the user can set credentials from the settings page.
    bool wifi_ok = wifi_wait_connected(15000);
    if (!wifi_ok) {
        ESP_LOGE(TAG, "Wi-Fi connection failed — starting provisioning AP");
        wifi_start_ap();
    } else {
        ESP_LOGI(TAG, "Wifi connected successfully!");
        time_sync_start();
        if (auth->is_logged_in && time_sync_wait(15000)) {
            esp_err_t refresh_err = spotify_refresh_access_token();
            if (refresh_err != ESP_OK) {
                ESP_LOGW(TAG, "Could not refresh Spotify access token: %s",
                         esp_err_to_name(refresh_err));
            }
        }
    }

    // The webserver runs in both modes: /submit when connected, /settings when
    // provisioning (or while connected, to change networks).
    webserver_t *webserver = webserver_create(APP_WEBSERVER_PORT);
    if (webserver_start(webserver) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver");
        setup_screen_show_status("Webserver failed", "Reboot to retry", NULL);
    } else if (wifi_ok) {
        char url[48];
        snprintf(url, sizeof(url), "%s:%d/submit", wifi_get_ip(), APP_WEBSERVER_PORT);
        setup_screen_show_status("Login at:", url, NULL);
    } else {
        char url[48];
        snprintf(url, sizeof(url), "%s:%d/settings", WIFI_AP_IP, APP_WEBSERVER_PORT);
        setup_screen_show_status("WiFi failed. Join AP:", CONFIG_PLAYER_AP_SSID, url);
    }

    // Logged in and online? start on the library; otherwise show setup.
    if (wifi_ok && auth->is_logged_in) {
        device->current_screen = SCREEN_ID_HOME;
    } else {
        device->current_screen = SCREEN_ID_SETUP;
    }
    ESP_LOGI(TAG, "Loading %s screen",
             device->current_screen == SCREEN_ID_HOME ? "library" : "setup");
    loadScreen(device->current_screen);

    // arm the screen sync loop (seeds itself with the screen just loaded; must
    // be created before the LVGL task starts)
    ui_state_init();

    /* Start rendering only after the whole UI tree exists. */
    ESP_ERROR_CHECK(lcd_start_lvgl());

    // populate the library; the request is serviced by the LVGL task, which by
    // now is running
    if (wifi_ok && auth->is_logged_in) {
        library_screen_request_refresh();
    }

    ESP_LOGI(TAG, "ESPSpotifyOS ready");
}

#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui.h"

#include "display/lcd.h"
#include "display/home_ui.h"
#include "display/ui_state.h"
#include "connectivity/wifi.h"
#include "server/http.h"
#include "data/state.h"
#include "utils/spotify.h"

static const char *TAG = "main";
#define APP_WEBSERVER_PORT 8080

// Starts non-blocking SNTP so time(NULL) returns real epoch time after sync
// (needed for correct proactive access-token refresh across reboots).
static void start_sntp(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP time sync started");
}

static bool wait_for_sntp_sync(uint32_t timeout_ms)
{
    TickType_t started_at = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        if (xTaskGetTickCount() - started_at >= timeout_ticks) {
            ESP_LOGW(TAG, "SNTP time sync timed out");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "SNTP time synchronized");
    return true;
}

// Shows where to point the browser (or what went wrong) on the SETUP screen's
// status label. Two centered lines max; safe pre-LVGL-task (like ui_init).
static void setup_ui_show_status(const char *line1, const char *line2)
{
    if (!objects.status) {
        return;
    }
    // widen + center the label (generator default is a narrow left-placed one)
    lv_obj_set_pos(objects.status, 0, 196);
    lv_obj_set_size(objects.status, 200, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(objects.status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    char text[80];
    if (line2) {
        snprintf(text, sizeof(text), "%s\n%s", line1, line2);
    } else {
        snprintf(text, sizeof(text), "%s", line1);
    }
    lv_label_set_text(objects.status, text);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // init state
    load_app_state();
    app_state_t *state = get_app_state();

    ESP_LOGI(TAG, "App state: logged_in=%d, current_screen=%d, wifi_ssid=%s",
             state->is_logged_in, state->current_screen, state->wifi_ssid);

    ESP_LOGI(TAG, "initializing display");
    ESP_ERROR_CHECK(lcd_init());
    lcd_backlight_init();
    lcd_backlight_set(20);

    // setup ui
    ESP_LOGI(TAG, "initializing ui");
    ui_init();

    // lets connect to the wifi
    if (!wifi_wait_connected(15000))
    {
        ESP_LOGE(TAG, "Wifi connection failed! Webserver will NOT start.");
        setup_ui_show_status("WiFi failed", "Reboot to retry");
    }
    else
    {
        ESP_LOGI(TAG, "Wifi connected successfully!");
        start_sntp();
        if (state->is_logged_in && wait_for_sntp_sync(15000)) {
            esp_err_t refresh_err = spotify_refresh_access_token();
            if (refresh_err != ESP_OK) {
                ESP_LOGW(TAG, "Could not refresh Spotify access token: %s",
                         esp_err_to_name(refresh_err));
            }
        }
        // and then setup and start webserver
        webserver_t *webserver = webserver_create(APP_WEBSERVER_PORT);
        if (webserver_start(webserver) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start webserver");
            setup_ui_show_status("Webserver failed", "Reboot to retry");
        }
        else
        {
            // tell the user where the login page lives
            char url[48];
            snprintf(url, sizeof(url), "%s:%d/submit", wifi_get_ip(), APP_WEBSERVER_PORT);
            setup_ui_show_status("Login at:", url);
        }
    }

    // if logged in to Spotify, load the Home screen; otherwise the login flow
    // (is_logged_in is the real indicator: the token placeholder is non-empty)
    if (state->is_logged_in)
    {
        ESP_LOGI(TAG, "Logged in to Spotify, loading Home screen");
        state->current_screen = SCREEN_ID_HOME;
    }
    else
    {
        ESP_LOGI(TAG, "Not logged in to Spotify, loading login screen");
        state->current_screen = SCREEN_ID_SETUP;
    }

    loadScreen(state->current_screen);

    // arm the state->screen sync loop (seeds itself with the screen just
    // loaded; must be created before the LVGL task starts)
    ui_state_init();

    /* Start rendering only after the whole UI tree exists. */
    ESP_ERROR_CHECK(lcd_start_lvgl());

    // populate the Home screen with the liked tracks list; the request is
    // serviced by the LVGL task, which by now is running
    if (state->is_logged_in) {
        home_ui_request_refresh();
    }

    ESP_LOGI(TAG, "ESPSpotifyOS ready");
}
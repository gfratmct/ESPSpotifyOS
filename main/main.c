#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "connectivity/wifi.h"
#include "core/time_sync.h"
#include "data/auth_state.h"
#include "data/device_state.h"
#include "data/state.h"
#include "display/display.h"
#include "net/spotify_auth.h"
#include "server/webserver.h"
#include "services/audio_output.h"
#include "services/player.h"
#include "storage/sd_storage.h"
#include "utils/storage.h"
#include "display/display.h"

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
    ESP_ERROR_CHECK(display_init());

    // Audio output + playback engine. Both are lazy: boot only mutes the amp
    // pin, and the DAC/I2S backend + player task start on first playback. This
    // keeps the boot heap free for Wi-Fi/TLS (SD is mounted lazily too).
    audio_output_init();
    player_init();

    // Connect to Wi-Fi. On failure, fall back to a provisioning access point
    // so the user can set credentials from the settings page.
    bool wifi_ok = wifi_wait_connected(15000);
    if (!wifi_ok) {
        ESP_LOGE(TAG, "Wi-Fi connection failed - starting provisioning AP");
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
    } else if (wifi_ok) {
        char url[48];
        snprintf(url, sizeof(url), "%s:%d/submit", wifi_get_ip(), APP_WEBSERVER_PORT);
        ESP_LOGI(TAG, "Login at: %s", url);
    } else {
        char url[48];
        snprintf(url, sizeof(url), "%s:%d/settings", WIFI_AP_IP, APP_WEBSERVER_PORT);
        ESP_LOGI(TAG, "WiFi failed. Join AP %s, then open %s",
                 CONFIG_PLAYER_AP_SSID, url);
    }

    // setup display
    display_init();
    lvgl_init();

    ESP_LOGI(TAG, "ESPSpotifyOS ready");
}

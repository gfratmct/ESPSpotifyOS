#include "wifi.h"

#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "data/device_state.h"

#define TAG "wifi"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAX_RETRY CONFIG_ESP_MAXIMUM_RETRY

static EventGroupHandle_t s_event_group;
static bool s_initialized;
static bool s_started;
static bool s_ap_active;
static char s_ip[16];

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    static int s_retry_num = 0;
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY && !s_ap_active) {
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "got ip:%s", s_ip);
        s_retry_num = 0;
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initializes the driver, default STA netif and event handlers exactly once.
static esp_err_t wifi_driver_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t err = wifi_driver_init();
    if (err != ESP_OK) {
        return err;
    }

    device_state_t *device = device_state_get();
    if (device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Zero-initialize wifi_config and copy credentials from state
    wifi_config_t wifi_config = {0};
    if (device->wifi_password[0] == '\0') {
        // open network: accept the weakest auth mode
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }
    strncpy((char *)wifi_config.sta.ssid, (const char *)device->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, (const char *)device->wifi_password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_started = true;
    ESP_LOGI(TAG, "wifi station started with SSID: %s", wifi_config.sta.ssid);
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    if (s_event_group == NULL) {
        return false;
    }
    return xEventGroupGetBits(s_event_group) & WIFI_CONNECTED_BIT;
}

bool wifi_wait_connected(uint32_t timeout_ms)
{
    if (!s_started) {
        if (wifi_start() != ESP_OK) {
            return false;
        }
    }
    EventBits_t bits = xEventGroupWaitBits(s_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

const char *wifi_get_ip(void)
{
    if (s_event_group == NULL || !wifi_is_connected()) {
        return "";
    }
    return s_ip;
}

esp_err_t wifi_start_ap(void)
{
    if (s_ap_active) {
        return ESP_OK;
    }

    esp_err_t err = wifi_driver_init();
    if (err != ESP_OK) {
        return err;
    }

    // Stop whatever STA was doing before switching to the provisioning AP.
    esp_wifi_stop();
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, CONFIG_PLAYER_AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(CONFIG_PLAYER_AP_SSID);
    ap_config.ap.max_connection = 2;
    ap_config.ap.channel = 1;

    if (CONFIG_PLAYER_AP_PASSWORD[0] == '\0') {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy((char *)ap_config.ap.password, CONFIG_PLAYER_AP_PASSWORD,
                sizeof(ap_config.ap.password) - 1);
    }

    // APSTA keeps the scan API available while the AP is up.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_active = true;
    ESP_LOGI(TAG, "provisioning AP '%s' started at %s", CONFIG_PLAYER_AP_SSID, WIFI_AP_IP);
    return ESP_OK;
}

bool wifi_is_ap_active(void)
{
    return s_ap_active;
}

int wifi_scan(wifi_ap_info_t *out, size_t max)
{
    if (!s_ap_active || !out || max == 0) {
        return -1;
    }

    wifi_scan_config_t scan = {0};
    esp_err_t err = esp_wifi_scan_start(&scan, true); // blocking
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return -1;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count == 0) {
        return 0;
    }
    if (count > max) count = max;

    wifi_ap_record_t *records = calloc(count, sizeof(*records));
    if (!records) {
        return -1;
    }

    uint16_t records_count = count;
    esp_wifi_scan_get_ap_records(&records_count, records);

    int found = 0;
    for (uint16_t i = 0; i < records_count && (size_t)found < max; i++) {
        wifi_ap_info_t *info = &out[found++];
        memset(info, 0, sizeof(*info));
        strncpy(info->ssid, (const char *)records[i].ssid, sizeof(info->ssid) - 1);
        info->rssi = records[i].rssi;
        info->secure = records[i].authmode != WIFI_AUTH_OPEN;
    }

    free(records);
    return found;
}

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default IP of the provisioning soft-AP (esp-netif default).
#define WIFI_AP_IP "192.168.4.1"

/* Start Wi-Fi station in the background (idempotent, non-blocking).
 * Returns ESP_OK once the driver is started; connection happens asynchronously. */
esp_err_t wifi_start(void);

bool wifi_is_connected(void);

/* Block up to timeout_ms waiting for a connection. Returns true on success. */
bool wifi_wait_connected(uint32_t timeout_ms);

/* IP address as string, or empty string if not connected. */
const char *wifi_get_ip(void);

// A nearby access point found by a scan.
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} wifi_ap_info_t;

/* Bring up the provisioning soft-AP (SSID/password from Kconfig). Switches to
 * APSTA so scans still work. Idempotent. */
esp_err_t wifi_start_ap(void);

/* Whether the provisioning soft-AP is active. */
bool wifi_is_ap_active(void);

/* Blocking scan of nearby networks (only valid while the provisioning AP is
 * active). Writes up to `max` entries into `out` and returns the number found,
 * or -1 on error. */
int wifi_scan(wifi_ap_info_t *out, size_t max);

#ifdef __cplusplus
}
#endif

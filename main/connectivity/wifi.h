#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "data/state.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start Wi-Fi station in the background (idempotent, non-blocking).
 * Returns ESP_OK once the driver is started; connection happens asynchronously. */
esp_err_t wifi_start(void);

bool wifi_is_connected(void);

/* Block up to timeout_ms waiting for a connection. Returns true on success. */
bool wifi_wait_connected(uint32_t timeout_ms);

/* IP address as string, or empty string if not connected. */
const char *wifi_get_ip(void);

#ifdef __cplusplus
}
#endif
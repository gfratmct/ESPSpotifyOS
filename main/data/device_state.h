#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif

// Device-level state, persisted in the NVS "device" namespace.
typedef struct {
    uint8_t wifi_ssid[32];     // array of bytes for SSID as esp-idf
    uint8_t wifi_password[64]; // array of bytes for password as esp-idf
    enum ScreensEnum current_screen;
} device_state_t;

/**
 * @brief Initialize the in-RAM device state with defaults (Kconfig Wi-Fi
 *        credentials and the setup screen).
 */
void device_state_reset(void);

/**
 * @brief Load the persisted device state into RAM.
 * @return ESP_OK if loaded, ESP_ERR_NVS_NOT_FOUND if none was stored (the
 *         defaults from device_state_reset() stay in place).
 */
esp_err_t device_state_load(void);

/**
 * @brief Persist the current in-RAM device state.
 */
esp_err_t device_state_save(void);

/**
 * @brief Restore the Wi-Fi credentials to the build-time Kconfig defaults
 *        (other fields are left untouched) and persist them.
 */
esp_err_t device_state_reset_wifi(void);

/**
 * @brief Access the global singleton device state.
 */
device_state_t *device_state_get(void);

#ifdef __cplusplus
}
#endif

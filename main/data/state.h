#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ui.h"
#include "screens.h"

#ifdef __cplusplus
extern "C"
{
#endif


  typedef struct
  {
    uint8_t spotify_token[256];
    uint8_t refresh_token[256];
    uint8_t wifi_ssid[32]; // array of bytes for SSID as esp-idf
    uint8_t wifi_password[64]; // array of bytes for password as esp-idf
    uint32_t token_expires_at;
    bool is_logged_in;
    enum ScreensEnum current_screen;
  } app_state_t;


  /**
   * @brief Load persistent state from flash into memory.
   */
  esp_err_t load_app_state(void);

  /**
   * @brief Save current memory state to flash.
   */
  esp_err_t save_app_state(void);

  /**
   * @brief Reset and erase persistent state in flash.
   */
  esp_err_t clear_app_state(void);

  /**
   * @brief Access global singleton app_state pointer.
   */
  app_state_t *get_app_state(void);

#ifdef __cplusplus
}
#endif
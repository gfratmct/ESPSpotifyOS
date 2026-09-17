#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Spotify session state, persisted in the NVS "auth" namespace.
typedef struct {
    uint8_t spotify_token[512]; // Spotify access tokens are ~340-360 char JWTs
    uint8_t refresh_token[512];
    uint32_t token_expires_at;
    bool is_logged_in;
} auth_state_t;

/**
 * @brief Initialize the in-RAM auth state with defaults.
 */
void auth_state_reset(void);

/**
 * @brief Load the persisted auth state into RAM.
 * @return ESP_OK if loaded, ESP_ERR_NVS_NOT_FOUND if none was stored (the
 *         defaults from auth_state_reset() stay in place).
 */
esp_err_t auth_state_load(void);

/**
 * @brief Persist the current in-RAM auth state.
 */
esp_err_t auth_state_save(void);

/**
 * @brief Access the global singleton auth state.
 */
auth_state_t *auth_state_get(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the Spotify authorize (login) URL the user opens in a browser
 *        to grant this device access (used by the /submit GET route).
 */
esp_err_t spotify_get_authorize_url(char *out, size_t out_size);

/**
 * @brief Exchange an OAuth authorization code (from the browser redirect) for
 *        an access/refresh token pair and persist them.
 *        Used by the /submit POST route once the user pastes the code back.
 */
esp_err_t spotify_exchange_code_for_token(const char *code);

/**
 * @brief Refresh the access token using the stored refresh token.
 *        Called automatically by the API helpers on expiry/401.
 */
esp_err_t spotify_refresh_access_token(void);

/**
 * @brief Drop the stored Spotify session: tokens are cleared and is_logged_in
 *        becomes false (the ui_state loop then switches to the login screen).
 *        Called automatically when Spotify rejects the refresh token.
 */
void spotify_logout(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Profile of the currently logged-in Spotify user (/v1/me)
typedef struct {
    char account_id[64];
    char display_name[64];
    char product[16];      // "premium" | "free" | "open"
    bool is_logged_in;
} spotify_auth_t;

typedef struct {
    char track_id[32];
    char track_name[128];
    char track_artist[128]; // first artist only
    char uri[128];          // spotify:track:<id>
    int duration_ms;
} spotify_track_t;

// ---- Authorization / authentication -----------------------------------------

/**
 * @brief Build the Spotify authorize (login) URL the user opens in a browser
 *        to grant this device access (used by the /submit GET route).
 */
esp_err_t spotify_get_authorize_url(char *out, size_t out_size);

/**
 * @brief Exchange an OAuth authorization code (from the browser redirect) for
 *        an access/refresh token pair and persist them into app_state.
 *        Used by the /submit POST route once the user pastes the code back.
 */
esp_err_t spotify_exchange_code_for_token(const char *code);

/**
 * @brief Refresh the access token using the stored refresh token.
 *        Called automatically by the API helpers below on expiry/401.
 */
esp_err_t spotify_refresh_access_token(void);

/**
 * @brief Drop the stored Spotify session: tokens are cleared, is_logged_in
 *        becomes false, the login screen is selected (the ui_state loop
 *        switches to it) and the state is persisted. Called automatically
 *        when Spotify rejects the refresh token (dead session).
 */
void spotify_logout(void);

// ---- Web API ------------------------------------------------------------------

/**
 * @brief Fetch the current user's profile (/v1/me). Also serves as an
 *        "is my token valid?" check.
 */
esp_err_t spotify_get_auth_info(spotify_auth_t *out);

/**
 * @brief Fetch a page of the user's saved (liked) tracks.
 *
 * @param limit         1..50 (response is trimmed via the `fields` query param,
 *                      but keep it small enough to fit in MAX_BODY_SIZE)
 * @param offset        paging offset
 * @param market        ISO country code ("GB"), or NULL to omit
 * @param out_tracks    caller-provided array of at least max_tracks
 * @param max_tracks    capacity of out_tracks
 * @param out_count     actual number of tracks parsed (may be < limit)
 * @param out_total     total saved tracks in the library (may be NULL)
 */
esp_err_t spotify_get_saved_tracks(int limit, int offset, const char *market,
                                   spotify_track_t *out_tracks, size_t max_tracks,
                                   size_t *out_count, int *out_total);

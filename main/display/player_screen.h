#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Builds the player screen and registers the library tap-to-play callback.
// Call once after ui_init() and before the LVGL task starts.
void player_screen_init(void);

// Refreshes the player screen from the player state. LVGL task context only,
// called by the ui_state loop.
void player_screen_sync(void);

#ifdef __cplusplus
}
#endif

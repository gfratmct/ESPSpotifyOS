#pragma once

// Arms the LVGL-timer loop that continuously mirrors app_state into the UI:
// whenever state->current_screen changes (login success, Spotify logout,
// future button actions...) the corresponding screen is loaded, and library
// refresh requests are serviced. The loop runs in the LVGL task context, so
// state may be changed safely from any task.
//
// Call once after the initial screen has been loaded (loadScreen) and before
// the LVGL task starts (lcd_start_lvgl).
void ui_state_init(void);

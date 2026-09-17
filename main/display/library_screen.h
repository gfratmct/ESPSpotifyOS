#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Builds the tab bar and wires the library list. Call once, after ui_init()
// and before the LVGL task starts.
void library_screen_init(void);

// Ask for the library panel to be repopulated. Safe to call from any task; the
// work happens in the LVGL task via the ui_state loop.
void library_screen_request_refresh(void);

// Consumes a pending request (true if one was set). Only the ui_state loop
// may call this (LVGL task context).
bool library_screen_take_refresh_request(void);

// Reloads the active tab. Must run in the LVGL task context.
void library_screen_refresh(void);

// Applies a finished background transfer to the UI. Must run in the LVGL task
// context; the ui_state loop calls it every tick.
void library_screen_service_transfer(void);

#ifdef __cplusplus
}
#endif

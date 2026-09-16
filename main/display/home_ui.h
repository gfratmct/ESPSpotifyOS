#pragma once

#include <stdbool.h>

// Ask for the Home screen's library panel to be repopulated with the user's
// liked tracks. Safe to call from any task; the work happens in the LVGL task
// via the ui_state loop.
void home_ui_request_refresh(void);

// Consumes a pending request (true if one was set). Only the ui_state loop
// may call this (LVGL task context).
bool home_ui_take_refresh_request(void);

// Populates the Home screen's library panel with the user's liked tracks.
// Must run in the LVGL task context. No-op if not logged in or if the Home
// screen hasn't been created yet.
void home_ui_refresh_library(void);

// Processes a finished background media import and updates the UI (top status
// label + the imported item's label). Must run in the LVGL task context; the
// ui_state loop calls it every tick.
void home_ui_service_import(void);

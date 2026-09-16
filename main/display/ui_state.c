#include "ui_state.h"

#include <esp_log.h>

#include "ui.h"
#include "screens.h"

#include "data/state.h"
#include "display/home_ui.h"

#define TAG "ui_state"
#define UI_STATE_POLL_PERIOD_MS 250

static enum ScreensEnum s_applied_screen = SCREEN_ID_SETUP;
static lv_timer_t *s_timer = NULL;

// The "loop": runs in the LVGL task context (only here is it safe to touch
// widgets) and keeps the display in sync with app_state.
static void ui_state_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    app_state_t *state = get_app_state();
    if (!state) {
        return;
    }

    // follow state->current_screen whenever it changes (from any task)
    if (state->current_screen != s_applied_screen) {
        ESP_LOGI(TAG, "Screen change %d -> %d", s_applied_screen, state->current_screen);
        s_applied_screen = state->current_screen;
        loadScreen(s_applied_screen);
    }

    // service library refresh requests (e.g. from the web login handler)
    if (home_ui_take_refresh_request()) {
        if (!state->is_logged_in) {
            return; // not logged in (anymore) — nothing to populate
        }
        if (state->current_screen != SCREEN_ID_HOME) {
            state->current_screen = SCREEN_ID_HOME;
            s_applied_screen = SCREEN_ID_HOME;
            loadScreen(SCREEN_ID_HOME);
        }
        home_ui_refresh_library();
    }

    // surface background media import results (long-press on a liked track)
    home_ui_service_import();
}

void ui_state_init(void)
{
    if (s_timer) {
        return;
    }
    // seed with the screen that is already on display so the first tick
    // doesn't pointlessly reload it
    app_state_t *state = get_app_state();
    s_applied_screen = state ? state->current_screen : SCREEN_ID_SETUP;
    s_timer = lv_timer_create(ui_state_timer_cb, UI_STATE_POLL_PERIOD_MS, NULL);
    ESP_LOGI(TAG, "UI state loop armed (period %ums)", UI_STATE_POLL_PERIOD_MS);
}

#include "ui_state.h"

#include <esp_log.h>

#include "ui.h"
#include "screens.h"

#include "data/auth_state.h"
#include "data/device_state.h"
#include "display/library_screen.h"

#define TAG "ui_state"
#define UI_STATE_POLL_PERIOD_MS 250

static enum ScreensEnum s_applied_screen = SCREEN_ID_SETUP;
static lv_timer_t *s_timer = NULL;

// The "loop": runs in the LVGL task context (only here is it safe to touch
// widgets) and keeps the display in sync with device/auth state.
static void ui_state_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    auth_state_t *auth = auth_state_get();
    device_state_t *device = device_state_get();
    if (!auth || !device) {
        return;
    }

    // A dropped Spotify session (e.g. a rejected refresh token) sends us back
    // to the setup screen; spotify_auth only flips is_logged_in.
    if (!auth->is_logged_in && device->current_screen != SCREEN_ID_SETUP) {
        device->current_screen = SCREEN_ID_SETUP;
    }

    // follow device->current_screen whenever it changes (from any task)
    if (device->current_screen != s_applied_screen) {
        ESP_LOGI(TAG, "Screen change %d -> %d", s_applied_screen, device->current_screen);
        s_applied_screen = device->current_screen;
        loadScreen(s_applied_screen);
    }

    // service library refresh requests (e.g. from the web login handler)
    if (library_screen_take_refresh_request()) {
        if (!auth->is_logged_in) {
            return; // not logged in (anymore) — nothing to populate
        }
        if (device->current_screen != SCREEN_ID_HOME) {
            device->current_screen = SCREEN_ID_HOME;
            s_applied_screen = SCREEN_ID_HOME;
            loadScreen(SCREEN_ID_HOME);
        }
        library_screen_refresh();
    }

    // surface background media transfer results (long-press on a liked track)
    library_screen_service_transfer();
}

void ui_state_init(void)
{
    if (s_timer) {
        return;
    }
    // seed with the screen that is already on display so the first tick
    // doesn't pointlessly reload it
    device_state_t *device = device_state_get();
    s_applied_screen = device ? device->current_screen : SCREEN_ID_SETUP;
    s_timer = lv_timer_create(ui_state_timer_cb, UI_STATE_POLL_PERIOD_MS, NULL);
    ESP_LOGI(TAG, "UI state loop armed (period %ums)", UI_STATE_POLL_PERIOD_MS);
}

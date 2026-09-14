#include "home_ui.h"

#include <esp_log.h>

#include "screens.h"
#include "fonts.h"

#include "data/state.h"
#include "utils/spotify.h"

#define TAG "home_ui"

// keep well under MAX_BODY_SIZE so the /me/tracks JSON response isn't truncated
#define HOME_UI_MAX_TRACKS 10
#define HOME_UI_ITEM_HEIGHT 24

// set from any task; consumed (cleared) by the ui_state loop in the LVGL task
static volatile bool s_refresh_requested = false;

static void clear_library_list(void)
{
    if (!objects.home_library_container) {
        return;
    }
    lv_obj_clean(objects.home_library_container); // also deletes objects.home_library_item
    objects.home_library_item = NULL;
}

static void add_library_label(int index, const char *text)
{
    lv_obj_t *item = lv_label_create(objects.home_library_container);
    lv_obj_set_pos(item, 0, index * HOME_UI_ITEM_HEIGHT);
    lv_label_set_long_mode(item, LV_LABEL_LONG_DOT); // ellipsize names wider than the panel
    lv_obj_set_size(item, 200, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(item, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(item, text);
}

void home_ui_refresh_library(void)
{
    if (!objects.home_library_container) {
        ESP_LOGW(TAG, "Home screen not created yet, skipping library refresh");
        return;
    }

    app_state_t *state = get_app_state();
    if (!state || !state->is_logged_in) {
        ESP_LOGW(TAG, "Not logged in to Spotify, skipping library refresh");
        return;
    }

    static spotify_track_t tracks[HOME_UI_MAX_TRACKS];
    size_t count = 0;
    int total = 0;
    esp_err_t err = spotify_get_saved_tracks(HOME_UI_MAX_TRACKS, 0, NULL,
                                             tracks, HOME_UI_MAX_TRACKS, &count, &total);

    // drop the placeholder / stale content before showing anything
    clear_library_list();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch saved tracks: %s", esp_err_to_name(err));
        state = get_app_state(); // re-read: the session may have died mid-fetch
        if (state && state->is_logged_in) {
            add_library_label(0, "Failed to load library");
        } // else: ui_state loop switches back to the login screen
        return;
    }

    if (count == 0) {
        add_library_label(0, "No liked songs yet");
        return;
    }

    char line[160];
    for (size_t i = 0; i < count; i++) {
        snprintf(line, sizeof(line), "%s - %s", tracks[i].track_name, tracks[i].track_artist);
        add_library_label((int)i, line);
    }

    ESP_LOGI(TAG, "Loaded %u/%d saved tracks into home screen", (unsigned)count, total);
}

void home_ui_request_refresh(void)
{
    // consumed by the ui_state loop in the LVGL task — safe from any task
    s_refresh_requested = true;
}

bool home_ui_take_refresh_request(void)
{
    bool requested = s_refresh_requested;
    s_refresh_requested = false;
    return requested; // must only be called from the LVGL task context
}

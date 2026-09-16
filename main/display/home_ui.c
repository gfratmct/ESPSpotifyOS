#include "home_ui.h"

#include <stdio.h>

#include <esp_log.h>

#include "screens.h"
#include "fonts.h"

#include "data/state.h"
#include "utils/spotify.h"

#define TAG "home_ui"

#define HOME_UI_PAGE_SIZE 24
#define HOME_UI_PANEL_WIDTH 200
#define HOME_UI_ITEM_PADDING_HOR 6
#define HOME_UI_ITEM_PADDING_VER 4
#define HOME_UI_ITEM_MIN_HEIGHT 24

// set from any task; consumed (cleared) by the ui_state loop in the LVGL task
static volatile bool s_refresh_requested = false;
static spotify_track_t s_tracks[HOME_UI_PAGE_SIZE];
static size_t s_loaded_count;
static int s_total_tracks;
static int s_page_offset;
static bool s_page_loading;
static lv_obj_t *s_active_item;

static void load_library_page(int offset, bool scroll_to_end);

static void clear_library_list(void)
{
    if (!objects.home_library_container) {
        return;
    }
    lv_obj_clean(objects.home_library_container); // also deletes objects.home_library_item
    objects.home_library_item = NULL;
    s_active_item = NULL;
}

static void library_item_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *item = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (s_active_item && s_active_item != item) {
            lv_obj_clear_state(s_active_item, LV_STATE_CHECKED);
        }
        s_active_item = item;
        lv_obj_add_state(item, LV_STATE_CHECKED);

        const spotify_track_t *track = lv_event_get_user_data(e);
        if (track) {
            ESP_LOGI(TAG, "Selected track: %s - %s", track->track_artist, track->track_name);
        }
    }
}

static int add_library_item(int y, const spotify_track_t *track)
{
    lv_obj_t *item = lv_btn_create(objects.home_library_container);
    lv_obj_set_pos(item, 0, y);
    lv_obj_set_width(item, HOME_UI_PANEL_WIDTH);
    lv_obj_set_style_pad_hor(item, HOME_UI_ITEM_PADDING_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(item, HOME_UI_ITEM_PADDING_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(item, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(item, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(item, lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED | LV_STATE_CHECKED);

    lv_obj_t *label = lv_label_create(item);
    int label_width = HOME_UI_PANEL_WIDTH - (2 * HOME_UI_ITEM_PADDING_HOR);
    lv_obj_set_width(label, label_width);
    lv_obj_set_pos(label, HOME_UI_ITEM_PADDING_HOR, HOME_UI_ITEM_PADDING_VER);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    char text[160];
    snprintf(text, sizeof(text), "%s - %s", track->track_name, track->track_artist);
    lv_label_set_text(label, text);
    lv_obj_update_layout(label);
    int height = lv_obj_get_height(label) + (2 * HOME_UI_ITEM_PADDING_VER);
    if (height < HOME_UI_ITEM_MIN_HEIGHT) {
        height = HOME_UI_ITEM_MIN_HEIGHT;
    }
    lv_obj_set_height(item, height);
    lv_obj_add_event_cb(item, library_item_event_handler, LV_EVENT_CLICKED, (void *)track);
    return height;
}

static void add_library_message(const char *text)
{
    lv_obj_t *label = lv_label_create(objects.home_library_container);
    lv_obj_set_size(label, HOME_UI_PANEL_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(label, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(label, text);
}

static void library_scroll_event_handler(lv_event_t *e)
{
    lv_obj_t *container = lv_event_get_target(e);
    if (s_page_loading || s_loaded_count == 0) {
        return;
    }

    if (lv_obj_get_scroll_bottom(container) <= HOME_UI_ITEM_MIN_HEIGHT &&
        s_page_offset + (int)s_loaded_count < s_total_tracks) {
        load_library_page(s_page_offset + (int)s_loaded_count, false);
    } else if (lv_obj_get_scroll_top(container) <= HOME_UI_ITEM_MIN_HEIGHT && s_page_offset > 0) {
        int previous_offset = s_page_offset - HOME_UI_PAGE_SIZE;
        load_library_page(previous_offset < 0 ? 0 : previous_offset, true);
    }
}

static void load_library_page(int offset, bool scroll_to_end)
{
    if (!objects.home_library_container || s_page_loading) {
        return;
    }

    s_page_loading = true;
    size_t count = 0;
    int total = 0;
    esp_err_t err = spotify_get_saved_tracks(HOME_UI_PAGE_SIZE, offset, NULL,
                                             s_tracks, HOME_UI_PAGE_SIZE, &count, &total);

    clear_library_list();
    s_loaded_count = 0;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch saved tracks: %s", esp_err_to_name(err));
        add_library_message("Failed to load library");
    } else if (count == 0) {
        add_library_message(offset == 0 ? "No liked songs yet" : "No more liked songs");
    } else {
        s_page_offset = offset;
        s_total_tracks = total;
        s_loaded_count = count;
        int y = 0;
        for (size_t i = 0; i < count; i++) {
            y += add_library_item(y, &s_tracks[i]);
        }
        ESP_LOGI(TAG, "Loaded tracks %d-%d of %d", offset + 1,
                 offset + (int)count, total);
    }

    lv_obj_update_layout(objects.home_library_container);
    lv_obj_scroll_to_y(objects.home_library_container,
                       scroll_to_end ? LV_COORD_MAX : 0, LV_ANIM_OFF);

    s_page_loading = false;
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

    s_page_offset = 0;
    s_total_tracks = 0;
    load_library_page(0, false);
    lv_obj_remove_event_cb_with_user_data(objects.home_library_container,
                                          library_scroll_event_handler, NULL);
    lv_obj_add_event_cb(objects.home_library_container, library_scroll_event_handler,
                        LV_EVENT_SCROLL_END, NULL);
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

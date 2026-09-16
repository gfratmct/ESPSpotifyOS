#include "home_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "screens.h"
#include "fonts.h"

#include "data/state.h"
#include "utils/media.h"
#include "utils/spotify.h"
#include "utils/stream_debug.h"

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

// ---- background media import ------------------------------------------------

// Written by the import worker task, read (after the state flag) by the LVGL
// task — same volatile-flag pattern the refresh request uses.
typedef enum {
    IMPORT_STATE_IDLE,
    IMPORT_STATE_RUNNING,
    IMPORT_STATE_DONE_OK,     // newly imported
    IMPORT_STATE_DONE_DUP,    // already in the library
    IMPORT_STATE_DONE_FAIL,
} media_import_state_t;

typedef struct {
    char query[320]; // "artist - title"
} media_import_request_t;

static volatile media_import_state_t s_import_state = IMPORT_STATE_IDLE;
static char s_import_error[96];
static QueueHandle_t s_import_queue;
static TaskHandle_t s_import_task;

// LVGL-task-only state
static lv_obj_t *s_import_item;
static char s_import_item_original[160];
static char s_top_label_original[64];
static lv_timer_t *s_status_timer;

static void request_import(const spotify_track_t *track, lv_obj_t *item);

static void load_library_page(int offset, bool scroll_to_end);

static void clear_library_list(void)
{
    if (!objects.home_library_container) {
        return;
    }
    lv_obj_clean(objects.home_library_container); // also deletes objects.home_library_item
    objects.home_library_item = NULL;
    s_active_item = NULL;
    s_import_item = NULL;
}

static void library_item_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *item = lv_event_get_target(e);
    const spotify_track_t *track = lv_event_get_user_data(e);

    if (code == LV_EVENT_CLICKED) {
        if (s_active_item && s_active_item != item) {
            lv_obj_clear_state(s_active_item, LV_STATE_CHECKED);
        }
        s_active_item = item;
        lv_obj_add_state(item, LV_STATE_CHECKED);

        if (track) {
            ESP_LOGI(TAG, "Selected track: %s - %s", track->track_artist, track->track_name);
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (track) {
            ESP_LOGI(TAG, "Long-press: importing %s - %s", track->track_artist, track->track_name);
        }
        request_import(track, item);
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
    lv_obj_add_event_cb(item, library_item_event_handler, LV_EVENT_LONG_PRESSED, (void *)track);
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

static void media_import_task(void *arg)
{
    (void)arg;
    for (;;) {
        media_import_request_t *req = NULL;
        if (xQueueReceive(s_import_queue, &req, portMAX_DELAY) != pdTRUE || !req) {
            continue;
        }

        ESP_LOGI(TAG, "importing: %s", req->query);
        bool created = false;
        media_track_t result;
        esp_err_t err = media_import_query(req->query, &result, &created);
        free(req);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "import ok: '%s' (id %d, %s)",
                     result.title, result.id, created ? "new" : "already present");
            s_import_state = created ? IMPORT_STATE_DONE_OK : IMPORT_STATE_DONE_DUP;
            stream_debug_track(result.id, result.encoding);
        } else {
            snprintf(s_import_error, sizeof(s_import_error), "%s", esp_err_to_name(err));
            ESP_LOGE(TAG, "import failed: %s", s_import_error);
            s_import_state = IMPORT_STATE_DONE_FAIL;
        }
    }
}

// Restores the top label to what it showed before an import status message.
static void home_status_revert_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer); // one-shot
    s_status_timer = NULL;
    if (objects.home_label_top) {
        lv_label_set_text(objects.home_label_top, s_top_label_original);
    }
}

// Sets the Home screen top label and, when revert_ms > 0, schedules it to go
// back to s_top_label_original. Cancels any pending revert first so a newer
// status message is never clobbered. LVGL task context only.
static void set_home_status(const char *text, uint32_t revert_ms)
{
    if (!objects.home_label_top) {
        return;
    }
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    lv_label_set_text(objects.home_label_top, text);
    if (revert_ms > 0) {
        s_status_timer = lv_timer_create(home_status_revert_cb, revert_ms, NULL);
    }
}

static void request_import(const spotify_track_t *track, lv_obj_t *item)
{
    if (!track) return;

    if (!media_client_configured()) {
        set_home_status("Media service not configured", 3000);
        return;
    }
    if (s_import_state == IMPORT_STATE_RUNNING) {
        set_home_status("Already importing...", 2000);
        return;
    }

    media_import_request_t *req = malloc(sizeof(*req));
    if (!req) return;
    snprintf(req->query, sizeof(req->query), "%s - %s", track->track_artist, track->track_name);

    // make sure the worker exists before queueing anything
    if (!s_import_queue) {
        s_import_queue = xQueueCreate(1, sizeof(media_import_request_t *));
    }
    if (!s_import_queue) {
        free(req);
        return;
    }
    if (!s_import_task) {
        if (xTaskCreate(media_import_task, "media_import", 8192, NULL, 4, &s_import_task) != pdPASS) {
            ESP_LOGE(TAG, "failed to create import task");
            free(req);
            set_home_status("Import unavailable", 3000);
            return;
        }
    }
    if (xQueueSend(s_import_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "import queue full, dropping request");
        free(req);
        set_home_status("Busy importing", 2000);
        return;
    }

    // remember the item + original label so we can mark it when done
    s_import_item = item;
    if (item) {
        lv_obj_t *label = lv_obj_get_child(item, 0);
        const char *text = label ? lv_label_get_text(label) : NULL;
        if (text) {
            strncpy(s_import_item_original, text, sizeof(s_import_item_original) - 1);
            lv_label_set_text_fmt(label, "%s ...", text);
        }
    }

    if (objects.home_label_top) {
        const char *t = lv_label_get_text(objects.home_label_top);
        strncpy(s_top_label_original, t ? t : "", sizeof(s_top_label_original) - 1);
    }
    s_import_state = IMPORT_STATE_RUNNING;
    set_home_status("Importing...", 0); // replaced on completion
}

void home_ui_service_import(void)
{
    switch (s_import_state) {
    case IMPORT_STATE_DONE_OK:
    case IMPORT_STATE_DONE_DUP:
    case IMPORT_STATE_DONE_FAIL:
        break;
    default:
        return;
    }

    media_import_state_t done = s_import_state;

    // mark the imported item in the list (it may have been paged away meanwhile)
    if (s_import_item && lv_obj_is_valid(s_import_item)) {
        lv_obj_t *label = lv_obj_get_child(s_import_item, 0);
        if (label) {
            const char *suffix = (done == IMPORT_STATE_DONE_OK) ? "[ok]"
                                : (done == IMPORT_STATE_DONE_DUP) ? "[dup]" : "[fail]";
            lv_label_set_text_fmt(label, "%s %s", s_import_item_original, suffix);
        }
    }
    s_import_item = NULL;

    const char *msg = (done == IMPORT_STATE_DONE_OK) ? "Imported"
                    : (done == IMPORT_STATE_DONE_DUP) ? "Already in library" : "Import failed";
    if (done == IMPORT_STATE_DONE_FAIL && s_import_error[0]) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s (%s)", msg, s_import_error);
        set_home_status(buf, 4000);
    } else {
        set_home_status(msg, 4000);
    }
    ESP_LOGI(TAG, "%s", msg);

    s_import_state = IMPORT_STATE_IDLE;
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

#include "display/player_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include "lvgl.h"

#include "fonts.h"
#include "screens.h"

#include "data/device_state.h"
#include "display/library_list.h"
#include "display/library_source.h"
#include "services/player.h"

#define TAG "player_screen"

#define COL_BG      lv_color_hex(0x111111)
#define COL_TEXT    lv_color_hex(0xFFFFFF)
#define COL_MUTED   lv_color_hex(0xB3B3B3)
#define COL_ACCENT  lv_color_hex(0x1DB954)
#define COL_ERROR   lv_color_hex(0xE05555)

static lv_obj_t *s_title;
static lv_obj_t *s_artist;
static lv_obj_t *s_error;
static lv_obj_t *s_bar;
static lv_obj_t *s_time;
static lv_obj_t *s_play_icon;
static lv_obj_t *s_vol_label;
static lv_obj_t *s_vol_slider;

static void format_time(int seconds, char *out, size_t out_size)
{
    if (seconds < 0) seconds = 0;
    snprintf(out, out_size, "%d:%02d", seconds / 60, seconds % 60);
}

// ---- callbacks ---------------------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    device_state_t *device = device_state_get();
    if (device) {
        device->current_screen = SCREEN_ID_HOME; // playback keeps running
    }
}

static void prev_cb(lv_event_t *e)
{
    (void)e;
    player_prev();
}

static void play_cb(lv_event_t *e)
{
    (void)e;
    player_toggle_pause();
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    player_next();
}

static void volume_cb(lv_event_t *e)
{
    player_set_volume((uint8_t)lv_slider_get_value(lv_event_get_target(e)));
}

// Tap-to-play: builds a player queue from the tapped library page.
static void on_item_tapped(const library_source_t *source, const library_item_t *items,
                           size_t count, size_t index)
{
    if (!source || source->origin == LIBRARY_ORIGIN_NONE || count == 0 || index >= count) {
        return;
    }

    player_track_t *queue = calloc(count, sizeof(*queue));
    if (!queue) return;

    for (size_t i = 0; i < count; i++) {
        strncpy(queue[i].title, items[i].title, sizeof(queue[i].title) - 1);
        strncpy(queue[i].artist, items[i].subtitle, sizeof(queue[i].artist) - 1);
        strncpy(queue[i].encoding, items[i].encoding, sizeof(queue[i].encoding) - 1);
        queue[i].duration_s = items[i].duration_s;

        if (source->origin == LIBRARY_ORIGIN_SERVER) {
            queue[i].source = PLAYER_TRACK_SERVER;
            queue[i].media_id = items[i].id;
        } else {
            queue[i].source = PLAYER_TRACK_FILE;
            strncpy(queue[i].path, items[i].transfer_key, sizeof(queue[i].path) - 1);
        }
    }

    esp_err_t err = player_play_queue(queue, count, index);
    free(queue);

    if (err == ESP_OK) {
        device_state_t *device = device_state_get();
        if (device) {
            device->current_screen = SCREEN_ID_PLAYER;
        }
    } else {
        ESP_LOGE(TAG, "play failed: %s", esp_err_to_name(err));
    }
}

// ---- UI construction ---------------------------------------------------------

static lv_obj_t *create_control(lv_obj_t *parent, const char *symbol, int x, int y, int size,
                                lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, size / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, COL_ACCENT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return label;
}

void player_screen_init(void)
{
    lv_obj_t *root = lv_obj_create(NULL);
    objects.player = root;
    lv_obj_set_size(root, 240, 320);
    lv_obj_set_style_bg_color(root, COL_BG, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // back to the library (playback continues)
    lv_obj_t *back = lv_btn_create(root);
    lv_obj_set_size(back, 34, 34);
    lv_obj_set_pos(back, 8, 8);
    lv_obj_set_style_radius(back, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *back_label = lv_label_create(back);
    lv_obj_set_style_text_font(back_label, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(back_label, COL_TEXT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    s_title = lv_label_create(root);
    lv_obj_set_width(s_title, 200);
    lv_obj_set_pos(s_title, 20, 52);
    lv_obj_set_style_text_font(s_title, &font_dm_sans_700_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_title, COL_TEXT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_title, "Nothing playing");

    s_artist = lv_label_create(root);
    lv_obj_set_width(s_artist, 200);
    lv_obj_set_pos(s_artist, 20, 120);
    lv_obj_set_style_text_font(s_artist, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_artist, COL_MUTED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_artist, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_artist, "");

    s_error = lv_label_create(root);
    lv_obj_set_width(s_error, 200);
    lv_obj_set_pos(s_error, 20, 152);
    lv_obj_set_style_text_font(s_error, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_error, COL_ERROR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_error, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_error, "");

    s_bar = lv_bar_create(root);
    lv_obj_set_size(s_bar, 200, 8);
    lv_obj_set_pos(s_bar, 20, 190);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_bar, COL_ACCENT, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    s_time = lv_label_create(root);
    lv_obj_set_width(s_time, 200);
    lv_obj_set_pos(s_time, 20, 204);
    lv_obj_set_style_text_font(s_time, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_time, COL_MUTED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_time, "0:00 / 0:00");

    create_control(root, LV_SYMBOL_PREV, 53, 236, 34, prev_cb);
    s_play_icon = create_control(root, LV_SYMBOL_PLAY, 98, 230, 44, play_cb);
    create_control(root, LV_SYMBOL_NEXT, 153, 236, 34, next_cb);

    s_vol_label = lv_label_create(root);
    lv_obj_set_width(s_vol_label, 200);
    lv_obj_set_pos(s_vol_label, 20, 282);
    lv_obj_set_style_text_font(s_vol_label, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_vol_label, COL_MUTED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_vol_label, "Volume");

    s_vol_slider = lv_slider_create(root);
    lv_obj_set_size(s_vol_slider, 200, 12);
    lv_obj_set_pos(s_vol_slider, 20, 300);
    lv_slider_set_range(s_vol_slider, 0, 100);
    lv_slider_set_value(s_vol_slider, player_get_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_vol_slider, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    library_list_set_tap_callback(on_item_tapped);
}

void player_screen_sync(void)
{
    if (!objects.player) {
        return;
    }

    player_status_t status;
    player_get_status(&status);

    const char *title = status.title[0] ? status.title : "Nothing playing";
    if (strcmp(lv_label_get_text(s_title), title) != 0) {
        lv_label_set_text(s_title, title);
    }
    if (strcmp(lv_label_get_text(s_artist), status.artist) != 0) {
        lv_label_set_text(s_artist, status.artist);
    }

    const char *error = status.state == PLAYER_ERROR ? player_error() : "";
    if (strcmp(lv_label_get_text(s_error), error) != 0) {
        lv_label_set_text(s_error, error);
    }

    int percent = 0;
    if (status.duration_s > 0 && status.position_s >= 0) {
        percent = (status.position_s * 100) / status.duration_s;
        if (percent > 100) percent = 100;
    }
    lv_bar_set_value(s_bar, percent, LV_ANIM_OFF);

    char now[16], total[16], text[40];
    format_time(status.position_s, now, sizeof(now));
    if (status.duration_s > 0) {
        format_time(status.duration_s, total, sizeof(total));
        snprintf(text, sizeof(text), "%s / %s", now, total);
    } else {
        snprintf(text, sizeof(text), "%s", now);
    }
    if (strcmp(lv_label_get_text(s_time), text) != 0) {
        lv_label_set_text(s_time, text);
    }

    const char *play_icon = status.state == PLAYER_PAUSED ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
    if (strcmp(lv_label_get_text(s_play_icon), play_icon) != 0) {
        lv_label_set_text(s_play_icon, play_icon);
    }

    // don't fight the user while they drag the slider
    if (!lv_obj_has_state(s_vol_slider, LV_STATE_PRESSED) &&
        lv_slider_get_value(s_vol_slider) != status.volume) {
        lv_slider_set_value(s_vol_slider, status.volume, LV_ANIM_OFF);
    }
    char vol_text[24];
    snprintf(vol_text, sizeof(vol_text), "Volume %u%%", status.volume);
    if (strcmp(lv_label_get_text(s_vol_label), vol_text) != 0) {
        lv_label_set_text(s_vol_label, vol_text);
    }
}

#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "connectivity/wifi.h"
#include "data/auth_state.h"
#include "data/device_state.h"
#include "display/display.h"
#include "input/buttons.h"
#include "net/media_client.h"
#include "net/spotify_api.h"
#include "net/spotify_auth.h"
#include "services/audio_output.h"
#include "services/player.h"
#include "services/transfer_manager.h"
#include "storage/sd_storage.h"

#define TAG "ui"

#define UI_PAGE_SIZE       6
// One server page plus one SD page on the first library page.
#define UI_LIB_CAP         (UI_PAGE_SIZE * 2)
#define UI_ACCENT_HEX      0x1DB954
#define UI_IMPORT_CAP      UI_PAGE_SIZE
#define UI_POLL_MS         250
#define UI_FETCH_STACK     8192
#define UI_FETCH_PRIO      3
#define UI_TAB_COUNT       5

typedef enum {
    LIB_SRC_SERVER = 0,
    LIB_SRC_SD,
} lib_src_t;

typedef struct {
    lib_src_t src;
    char title[96];
    char artist[64];
    int media_id;
    char encoding[16];
    char path[288];
    int duration_s;
} lib_item_t;

typedef struct {
    char title[96];
    char artist[64];
} import_item_t;

typedef enum {
    FETCH_NONE = 0,
    FETCH_LIBRARY,
    FETCH_IMPORT,
} fetch_cmd_t;

static lv_obj_t *s_header;
static lv_obj_t *s_text;
static lv_obj_t *s_status;
static lv_obj_t *s_rows[UI_PAGE_SIZE];
static lv_obj_t *s_row_title[UI_PAGE_SIZE];
static lv_obj_t *s_row_artist[UI_PAGE_SIZE];
static lv_obj_t *s_player;
static lv_obj_t *s_play_title;
static lv_obj_t *s_play_artist;
static lv_obj_t *s_play_bar;
static lv_obj_t *s_play_time;
static lv_obj_t *s_play_meta;

typedef struct {
    char title[96];
    char artist[64];
} row_snap_t;

static row_snap_t s_row_snap[UI_PAGE_SIZE];

static enum ScreensEnum s_screen = SCREEN_ID_SETUP;
static bool s_setup_locked;

// Guarded by s_data_lock (shared between the LVGL task and the fetch task).
static lib_item_t s_lib[UI_LIB_CAP];
static size_t s_lib_count;
static int s_lib_total;
static int s_lib_offset;
static int s_lib_sel;
static bool s_lib_loading;
static char s_lib_err[48];

static import_item_t s_imp[UI_IMPORT_CAP];
static size_t s_imp_count;
static int s_imp_total;
static int s_imp_offset;
static int s_imp_sel;
static bool s_imp_loading;
static char s_imp_err[48];

// Large scratch buffers live in .bss: the LVGL task (8 KB) and the fetch task
// (8 KB) cannot hold them on their stacks.
static lib_item_t s_lib_scratch[UI_LIB_CAP];
static import_item_t s_imp_scratch[UI_IMPORT_CAP];
static media_track_t s_media_scratch[UI_PAGE_SIZE];
static sd_file_t s_sd_scratch[UI_PAGE_SIZE];
static spotify_track_t s_spotify_scratch[UI_PAGE_SIZE];
static player_track_t s_play_queue[UI_LIB_CAP];

static bool s_logout_confirm;
static transfer_state_t s_last_xfer = TRANSFER_IDLE;
static player_state_t s_last_player_state = PLAYER_IDLE;
static char s_last_player_title[64];

static QueueHandle_t s_fetch_q;
static SemaphoreHandle_t s_data_lock;

static const enum ScreensEnum s_tab_order[UI_TAB_COUNT] = {
    SCREEN_ID_SETUP,
    SCREEN_ID_LIBRARY,
    SCREEN_ID_IMPORT,
    SCREEN_ID_SETTINGS,
    SCREEN_ID_PLAYER,
};

static const char *screen_name(enum ScreensEnum id)
{
    switch (id) {
    case SCREEN_ID_SETUP: return "Setup";
    case SCREEN_ID_LIBRARY: return "Library";
    case SCREEN_ID_IMPORT: return "Import";
    case SCREEN_ID_SETTINGS: return "Settings";
    case SCREEN_ID_PLAYER: return "Player";
    default: return "?";
    }
}

static const char *player_state_name(player_state_t st)
{
    switch (st) {
    case PLAYER_IDLE: return "IDLE";
    case PLAYER_PLAYING: return "PLAYING";
    case PLAYER_PAUSED: return "PAUSED";
    case PLAYER_ERROR: return "ERROR";
    default: return "?";
    }
}

static const char *transfer_state_name(transfer_state_t st)
{
    switch (st) {
    case TRANSFER_IDLE: return "IDLE";
    case TRANSFER_RUNNING: return "RUNNING";
    case TRANSFER_DONE_NEW: return "DONE_NEW";
    case TRANSFER_DONE_DUPLICATE: return "DONE_DUPLICATE";
    case TRANSFER_DONE_FAILED: return "DONE_FAILED";
    default: return "?";
    }
}

static int tab_index(enum ScreensEnum id)
{
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        if (s_tab_order[i] == id) {
            return i;
        }
    }
    return 0;
}

// Returns NULL when Setup is not required, otherwise the reason.
static const char *setup_lock_reason(void)
{
    if (wifi_is_ap_active()) {
        return "provisioning AP active";
    }
    if (!wifi_is_connected()) {
        return "Wi-Fi not connected";
    }
    auth_state_t *auth = auth_state_get();
    if (!auth || !auth->is_logged_in) {
        return "Spotify not logged in";
    }
    return NULL;
}

static void persist_screen(void)
{
    device_state_t *dev = device_state_get();
    if (dev->current_screen != s_screen) {
        dev->current_screen = s_screen;
        esp_err_t err = device_state_save();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "saving current screen failed: %s", esp_err_to_name(err));
        }
    }
}

static void request_fetch(fetch_cmd_t cmd)
{
    if (!s_fetch_q) {
        return;
    }
    ESP_LOGI(TAG, "fetch requested: %s", cmd == FETCH_LIBRARY ? "library" : "import");
    xQueueOverwrite(s_fetch_q, &cmd);
}

static void set_screen(enum ScreensEnum next, const char *reason)
{
    if (s_setup_locked && next != SCREEN_ID_SETUP) {
        ESP_LOGW(TAG, "tab change to %s blocked: %s", screen_name(next),
                 setup_lock_reason() ? setup_lock_reason() : "setup locked");
        return;
    }
    if (next != s_screen) {
        ESP_LOGI(TAG, "tab %s -> %s (%s)", screen_name(s_screen), screen_name(next), reason);
    }
    s_screen = next;
    s_logout_confirm = false;
    persist_screen();

    if (next == SCREEN_ID_LIBRARY) {
        request_fetch(FETCH_LIBRARY);
    } else if (next == SCREEN_ID_IMPORT) {
        request_fetch(FETCH_IMPORT);
    }
}

static void next_tab(void)
{
    if (s_setup_locked) {
        ESP_LOGW(TAG, "next tab ignored, locked on Setup: %s",
                 setup_lock_reason() ? setup_lock_reason() : "?");
        return;
    }
    int idx = tab_index(s_screen);
    set_screen(s_tab_order[(idx + 1) % UI_TAB_COUNT], "CENTER");
}

static void adjust_volume(int delta)
{
    int vol = (int)player_get_volume() + delta;
    if (vol < 0) {
        vol = 0;
    }
    if (vol > 100) {
        vol = 100;
    }
    player_set_volume((uint8_t)vol);
    device_state_set_volume((uint8_t)vol);
    ESP_LOGI(TAG, "volume -> %d%%", vol);
}

static void toggle_pause(void)
{
    player_status_t st;
    player_get_status(&st);
    if (st.count == 0) {
        ESP_LOGW(TAG, "play/pause ignored: queue empty (pick a track in Library)");
        return;
    }
    ESP_LOGI(TAG, "play/pause (was %s)", player_state_name(st.state));
    player_toggle_pause();
}

static void play_library_selection(void)
{
    if (xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "play: data lock busy");
        return;
    }
    if (s_lib_count == 0 || s_lib_sel < 0 || (size_t)s_lib_sel >= s_lib_count) {
        ESP_LOGW(TAG, "play: nothing selected (count=%u sel=%d)",
                 (unsigned)s_lib_count, s_lib_sel);
        xSemaphoreGive(s_data_lock);
        return;
    }

    size_t n = 0;
    size_t start = 0;
    for (size_t i = 0; i < s_lib_count && n < UI_LIB_CAP; i++) {
        const lib_item_t *it = &s_lib[i];
        player_track_t *t = &s_play_queue[n];
        memset(t, 0, sizeof(*t));
        strncpy(t->title, it->title, sizeof(t->title) - 1);
        strncpy(t->artist, it->artist, sizeof(t->artist) - 1);
        t->duration_s = it->duration_s;
        if (it->src == LIB_SRC_SERVER) {
            t->source = PLAYER_TRACK_SERVER;
            t->media_id = it->media_id;
            strncpy(t->encoding, it->encoding, sizeof(t->encoding) - 1);
        } else {
            t->source = PLAYER_TRACK_FILE;
            strncpy(t->path, it->path, sizeof(t->path) - 1);
        }
        if ((int)i == s_lib_sel) {
            start = n;
        }
        n++;
    }
    xSemaphoreGive(s_data_lock);

    const player_track_t *sel = &s_play_queue[start];
    ESP_LOGI(TAG, "play: '%s' (%s id=%d path=%s) queue=%u start=%u",
             sel->title, sel->source == PLAYER_TRACK_SERVER ? "server" : "sd",
             sel->media_id, sel->path[0] ? sel->path : "-", (unsigned)n, (unsigned)start);

    esp_err_t err = player_play_queue(s_play_queue, n, start);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "player_play_queue failed: %s", esp_err_to_name(err));
        return;
    }
    set_screen(SCREEN_ID_PLAYER, "track started");
}

static void import_selection(void)
{
    if (xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "import: data lock busy");
        return;
    }
    if (s_imp_count == 0 || s_imp_sel < 0 || (size_t)s_imp_sel >= s_imp_count) {
        ESP_LOGW(TAG, "import: nothing selected (count=%u sel=%d)",
                 (unsigned)s_imp_count, s_imp_sel);
        xSemaphoreGive(s_data_lock);
        return;
    }
    char query[192];
    snprintf(query, sizeof(query), "%s - %s",
             s_imp[s_imp_sel].artist, s_imp[s_imp_sel].title);
    xSemaphoreGive(s_data_lock);

    ESP_LOGI(TAG, "import: '%s'", query);
    if (!transfer_manager_import(query)) {
        ESP_LOGW(TAG, "import not queued (transfer busy or worker failed to start)");
    }
}

static void render_setup(char *body, size_t body_sz, char *status, size_t status_sz)
{
    if (wifi_is_ap_active()) {
        snprintf(body, body_sz, "AP: %s\n%s:%d\n/settings",
                 CONFIG_PLAYER_AP_SSID, WIFI_AP_IP, CONFIG_PLAYER_WEBSERVER_PORT);
        snprintf(status, status_sz, "Set WiFi in browser");
        return;
    }

    if (!wifi_is_connected()) {
        snprintf(body, body_sz, "WiFi offline\nWaiting...");
        snprintf(status, status_sz, "No network");
        return;
    }

    const char *ip = wifi_get_ip();
    auth_state_t *auth = auth_state_get();
    if (!auth->is_logged_in) {
        snprintf(body, body_sz, "Spotify login:\n%s:%d\n/submit",
                 ip, CONFIG_PLAYER_WEBSERVER_PORT);
        snprintf(status, status_sz, "Login required");
        return;
    }

    snprintf(body, body_sz, "WiFi %s\nSpotify OK", ip);
    snprintf(status, status_sz, "C:next tab");
}

// Copies the visible library page. message is set when there is nothing to list.
static void snapshot_library(size_t *count, int *sel, char *status, size_t status_sz,
                             char *message, size_t message_sz)
{
    *count = 0;
    *sel = 0;
    message[0] = '\0';
    status[0] = '\0';

    if (xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        snprintf(message, message_sz, "...");
        return;
    }

    if (s_lib_loading && s_lib_count == 0) {
        snprintf(message, message_sz, "Loading...");
        snprintf(status, status_sz, "C: next tab");
    } else if (s_lib_err[0] && s_lib_count == 0) {
        snprintf(message, message_sz, "%s", s_lib_err);
        snprintf(status, status_sz, "C: next tab");
    } else if (s_lib_count == 0) {
        snprintf(message, message_sz, "Empty library\nImport tracks or insert an SD card");
        snprintf(status, status_sz, "C: next tab");
    } else {
        // The first library page appends an SD page, so the list can be longer
        // than the six rows on screen. Slide a window so the cursor stays visible.
        const size_t n = s_lib_count;
        int cursor = s_lib_sel;
        if (cursor < 0 || (size_t)cursor >= n) {
            cursor = 0;
        }
        size_t start = 0;
        if (n > UI_PAGE_SIZE && (size_t)cursor + 1 > UI_PAGE_SIZE) {
            start = (size_t)cursor + 1 - UI_PAGE_SIZE;
        }
        size_t shown = n - start;
        if (shown > UI_PAGE_SIZE) {
            shown = UI_PAGE_SIZE;
        }
        for (size_t i = 0; i < shown; i++) {
            const lib_item_t *it = &s_lib[start + i];
            strncpy(s_row_snap[i].title, it->title, sizeof(s_row_snap[i].title) - 1);
            s_row_snap[i].title[sizeof(s_row_snap[i].title) - 1] = '\0';
            if (it->artist[0]) {
                strncpy(s_row_snap[i].artist, it->artist, sizeof(s_row_snap[i].artist) - 1);
            } else if (it->src == LIB_SRC_SERVER) {
                strncpy(s_row_snap[i].artist, "Server", sizeof(s_row_snap[i].artist) - 1);
            } else {
                strncpy(s_row_snap[i].artist, "SD", sizeof(s_row_snap[i].artist) - 1);
            }
            s_row_snap[i].artist[sizeof(s_row_snap[i].artist) - 1] = '\0';
        }
        *count = shown;
        *sel = cursor - (int)start;
        snprintf(status, status_sz, "%d/%d   R: play",
                 s_lib_offset + s_lib_sel + 1,
                 s_lib_total > 0 ? s_lib_total : (int)s_lib_count);
    }
    xSemaphoreGive(s_data_lock);
}

static void snapshot_import(size_t *count, int *sel, char *status, size_t status_sz,
                            char *message, size_t message_sz)
{
    *count = 0;
    *sel = 0;
    message[0] = '\0';
    status[0] = '\0';

    transfer_state_t xfer = transfer_manager_state();
    if (xfer == TRANSFER_RUNNING) {
        snprintf(message, message_sz, "Importing...\nPlease wait");
        snprintf(status, status_sz, "C: next tab");
        return;
    }
    if (xfer == TRANSFER_DONE_NEW || xfer == TRANSFER_DONE_DUPLICATE) {
        snprintf(message, message_sz, "Import done\n(%s)",
                 xfer == TRANSFER_DONE_NEW ? "new" : "already there");
        snprintf(status, status_sz, "R: dismiss");
        return;
    }
    if (xfer == TRANSFER_DONE_FAILED) {
        snprintf(message, message_sz, "Import failed\n%s", transfer_manager_error());
        snprintf(status, status_sz, "R: dismiss");
        return;
    }

    if (xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        snprintf(message, message_sz, "...");
        return;
    }
    if (s_imp_loading && s_imp_count == 0) {
        snprintf(message, message_sz, "Loading liked tracks...");
        snprintf(status, status_sz, "C: next tab");
    } else if (s_imp_err[0] && s_imp_count == 0) {
        snprintf(message, message_sz, "%s", s_imp_err);
        snprintf(status, status_sz, "C: next tab");
    } else if (s_imp_count == 0) {
        snprintf(message, message_sz, "No liked tracks");
        snprintf(status, status_sz, "C: next tab");
    } else {
        size_t n = s_imp_count;
        if (n > UI_PAGE_SIZE) {
            n = UI_PAGE_SIZE;
        }
        for (size_t i = 0; i < n; i++) {
            strncpy(s_row_snap[i].title, s_imp[i].title, sizeof(s_row_snap[i].title) - 1);
            s_row_snap[i].title[sizeof(s_row_snap[i].title) - 1] = '\0';
            strncpy(s_row_snap[i].artist, s_imp[i].artist, sizeof(s_row_snap[i].artist) - 1);
            s_row_snap[i].artist[sizeof(s_row_snap[i].artist) - 1] = '\0';
        }
        *count = n;
        *sel = s_imp_sel;
        snprintf(status, status_sz, "%d/%d   R: import",
                 s_imp_offset + s_imp_sel + 1,
                 s_imp_total > 0 ? s_imp_total : (int)s_imp_count);
    }
    xSemaphoreGive(s_data_lock);
}

static void render_settings(char *body, size_t body_sz, char *status, size_t status_sz)
{
    if (s_logout_confirm) {
        snprintf(body, body_sz, "Logout Spotify?\nR: yes\nL: cancel");
        snprintf(status, status_sz, "Confirm");
        return;
    }
    const char *ip = wifi_is_connected() ? wifi_get_ip()
                     : (wifi_is_ap_active() ? WIFI_AP_IP : "-");
    snprintf(body, body_sz, "IP %s\nVolume %u%%\nUp / Down to change\nRight: log out",
             ip, (unsigned)player_get_volume());
    snprintf(status, status_sz, "Wi-Fi: /settings");
}

static void ui_set_label_if_changed(lv_obj_t *label, const char *text)
{
    const char *cur = lv_label_get_text(label);
    if (cur && strcmp(cur, text) == 0) {
        return;
    }
    lv_label_set_text(label, text);
}

static void obj_set_hidden(lv_obj_t *obj, bool hidden)
{
    if (lv_obj_is_hidden(obj) == hidden) {
        return;
    }
    lv_obj_set_hidden(obj, hidden);
}

static const lv_font_t *ui_font_body(void)
{
#if LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return LV_FONT_DEFAULT;
#endif
}

static const lv_font_t *ui_font_title(void)
{
#if LV_FONT_MONTSERRAT_20
    return &lv_font_montserrat_20;
#else
    return ui_font_body();
#endif
}

// Message screens (setup, settings, empty lists) use the wrapping text label.
static void ui_show_text(const char *text)
{
    obj_set_hidden(s_text, false);
    obj_set_hidden(s_player, true);
    for (int i = 0; i < UI_PAGE_SIZE; i++) {
        obj_set_hidden(s_rows[i], true);
    }
    ui_set_label_if_changed(s_text, text);
}

// Selected title scrolls; the others clip so six animations do not repaint the panel.
static void ui_show_rows(size_t count, int sel)
{
    obj_set_hidden(s_text, true);
    obj_set_hidden(s_player, true);
    if (sel < 0 || (size_t)sel >= count) {
        sel = 0;
    }
    for (int i = 0; i < UI_PAGE_SIZE; i++) {
        if ((size_t)i >= count) {
            obj_set_hidden(s_rows[i], true);
            continue;
        }
        obj_set_hidden(s_rows[i], false);
        ui_set_label_if_changed(s_row_title[i], s_row_snap[i].title);
        ui_set_label_if_changed(s_row_artist[i], s_row_snap[i].artist);

        const bool selected = (i == sel);
        const lv_label_long_mode_t mode = selected ? LV_LABEL_LONG_SCROLL_CIRCULAR
                                                   : LV_LABEL_LONG_CLIP;
        if (lv_label_get_long_mode(s_row_title[i]) != mode) {
            lv_label_set_long_mode(s_row_title[i], mode);
        }
        lv_obj_set_style_bg_opa(s_rows[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_row_title[i],
                                    selected ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_text_color(s_row_artist[i],
                                    selected ? lv_color_hex(0x083318) : lv_color_hex(0xB3B3B3), 0);
    }
}

static void ui_show_player(const player_status_t *st)
{
    obj_set_hidden(s_text, true);
    obj_set_hidden(s_player, false);
    for (int i = 0; i < UI_PAGE_SIZE; i++) {
        obj_set_hidden(s_rows[i], true);
    }

    ui_set_label_if_changed(s_play_title, st->title[0] ? st->title : "-");
    ui_set_label_if_changed(s_play_artist, st->artist[0] ? st->artist : "");

    int pct = 0;
    if (st->duration_s > 0 && st->position_s > 0) {
        pct = st->position_s * 100 / st->duration_s;
        if (pct > 100) {
            pct = 100;
        }
    }
    if (lv_bar_get_value(s_play_bar) != pct) {
        lv_bar_set_value(s_play_bar, pct, LV_ANIM_OFF);
    }

    // position_s can be 0 before the decoder reports a timestamp.
    const int pos = st->position_s > 0 ? st->position_s : 0;
    const int dur = st->duration_s > 0 ? st->duration_s : 0;
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%d:%02d / %d:%02d",
             pos / 60, pos % 60, dur / 60, dur % 60);
    ui_set_label_if_changed(s_play_time, time_buf);

    char meta[64];
    snprintf(meta, sizeof(meta), "Vol %u%%    %u / %u",
             (unsigned)st->volume,
             (unsigned)(st->index + 1), (unsigned)st->count);
    ui_set_label_if_changed(s_play_meta, meta);
}

static void ui_refresh_labels(void)
{
    char message[160];
    char status[64];
    message[0] = '\0';
    status[0] = '\0';

    size_t rows = 0;
    int sel = 0;
    bool list = false;
    bool player = false;

    switch (s_screen) {
    case SCREEN_ID_SETUP:
        render_setup(message, sizeof(message), status, sizeof(status));
        break;
    case SCREEN_ID_LIBRARY:
        snapshot_library(&rows, &sel, status, sizeof(status), message, sizeof(message));
        list = (message[0] == '\0' && rows > 0);
        break;
    case SCREEN_ID_IMPORT:
        snapshot_import(&rows, &sel, status, sizeof(status), message, sizeof(message));
        list = (message[0] == '\0' && rows > 0);
        break;
    case SCREEN_ID_SETTINGS:
        render_settings(message, sizeof(message), status, sizeof(status));
        break;
    case SCREEN_ID_PLAYER: {
        player_status_t st;
        player_get_status(&st);
        if (st.count == 0) {
            snprintf(message, sizeof(message), "Nothing queued\nPick a track in Library");
            snprintf(status, sizeof(status), "C: next tab");
        } else {
            player = true;
            ui_show_player(&st);
            snprintf(status, sizeof(status), "%s    hold C: pause", player_state_name(st.state));
        }
        break;
    }
    }

    if (!player) {
        if (list) {
            ui_show_rows(rows, sel);
        } else {
            ui_show_text(message);
        }
    }

    char title[40];
    snprintf(title, sizeof(title), "%d/%d  %s%s", tab_index(s_screen) + 1, UI_TAB_COUNT,
             screen_name(s_screen), s_setup_locked ? "  locked" : "");
    ui_set_label_if_changed(s_header, title);
    ui_set_label_if_changed(s_status, status);
}

// Up/Down move the cursor and roll over to the next/previous page at the
// edges; Left jumps back one page.
static void handle_list_nav(button_id_t id, int *sel, size_t count,
                            int *offset, int total, fetch_cmd_t refetch)
{
    if (count == 0) {
        ESP_LOGI(TAG, "list empty, %s ignored", button_name(id));
        return;
    }
    bool refetch_needed = false;
    if (xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    if (id == BTN_UP) {
        if (*sel > 0) {
            (*sel)--;
        } else if (*offset > 0) {
            *offset = (*offset - UI_PAGE_SIZE) < 0 ? 0 : *offset - UI_PAGE_SIZE;
            *sel = UI_PAGE_SIZE - 1;
            refetch_needed = true;
        }
    } else if (id == BTN_DOWN) {
        if ((size_t)(*sel + 1) < count) {
            (*sel)++;
        } else if (total > 0 && *offset + UI_PAGE_SIZE < total) {
            *offset += UI_PAGE_SIZE;
            *sel = 0;
            refetch_needed = true;
        }
    } else if (id == BTN_LEFT) {
        if (*offset > 0) {
            *offset = (*offset - UI_PAGE_SIZE) < 0 ? 0 : *offset - UI_PAGE_SIZE;
            *sel = 0;
            refetch_needed = true;
        }
    }
    const int new_sel = *sel;
    const int new_offset = *offset;
    xSemaphoreGive(s_data_lock);

    ESP_LOGI(TAG, "list %s -> sel=%d offset=%d total=%d", button_name(id),
             new_sel, new_offset, total);
    if (refetch_needed) {
        request_fetch(refetch);
    }
}

// The melody writes PCM, so it cannot run on the LVGL task or beside the player.
static TaskHandle_t s_melody_task;

static void aux_melody_task(void *arg)
{
    (void)arg;

    player_status_t st;
    player_get_status(&st);
    if (st.state == PLAYER_PLAYING || st.state == PLAYER_PAUSED) {
        ESP_LOGI(TAG, "stopping playback for AUX melody");
        player_stop();
        for (int i = 0; i < 60 && (st.state == PLAYER_PLAYING || st.state == PLAYER_PAUSED); i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
            player_get_status(&st);
        }
        if (st.state == PLAYER_PLAYING || st.state == PLAYER_PAUSED) {
            ESP_LOGW(TAG, "player still busy, AUX melody skipped");
            s_melody_task = NULL;
            vTaskDelete(NULL);
            return;
        }
    }

    audio_output_test_melody();
    s_melody_task = NULL;
    vTaskDelete(NULL);
}

static void start_aux_melody(void)
{
    if (s_melody_task) {
        ESP_LOGW(TAG, "AUX melody already playing");
        return;
    }
    if (xTaskCreate(aux_melody_task, "aux_melody", 4096, NULL, 3, &s_melody_task) != pdPASS) {
        s_melody_task = NULL;
        ESP_LOGE(TAG, "AUX melody task failed");
    }
}

// Controls:
//   CENTER       next tab (everywhere)
//   CENTER_LONG  play/pause (everywhere)
//   UP held 5 s  AUX test melody (everywhere)
//   UP/DOWN      move in lists, volume in Player/Settings
//   RIGHT        action: play (Library), import (Import), logout (Settings),
//                next track (Player)
//   LEFT         previous page (lists), cancel (Settings), previous track (Player)
static void handle_button(button_id_t id)
{
    ESP_LOGI(TAG, "button %s on %s%s", button_name(id), screen_name(s_screen),
             s_setup_locked ? " (locked)" : "");

    if (id == BTN_CENTER) {
        next_tab();
        return;
    }
    if (id == BTN_CENTER_LONG) {
        toggle_pause();
        return;
    }
    if (id == BTN_UP_LONG) {
        start_aux_melody();
        return;
    }

    switch (s_screen) {
    case SCREEN_ID_SETUP:
        ESP_LOGI(TAG, "Setup tab has no actions; finish Wi-Fi/login in the browser");
        break;

    case SCREEN_ID_LIBRARY:
        if (id == BTN_RIGHT) {
            play_library_selection();
        } else {
            handle_list_nav(id, &s_lib_sel, s_lib_count, &s_lib_offset,
                            s_lib_total, FETCH_LIBRARY);
        }
        break;

    case SCREEN_ID_IMPORT: {
        transfer_state_t xfer = transfer_manager_state();
        if (xfer == TRANSFER_DONE_NEW || xfer == TRANSFER_DONE_DUPLICATE ||
            xfer == TRANSFER_DONE_FAILED) {
            if (id == BTN_RIGHT || id == BTN_LEFT) {
                ESP_LOGI(TAG, "transfer result dismissed");
                transfer_manager_clear();
            }
            break;
        }
        if (xfer == TRANSFER_RUNNING) {
            ESP_LOGI(TAG, "transfer running, %s ignored", button_name(id));
            break;
        }
        if (id == BTN_RIGHT) {
            import_selection();
        } else {
            handle_list_nav(id, &s_imp_sel, s_imp_count, &s_imp_offset,
                            s_imp_total, FETCH_IMPORT);
        }
        break;
    }

    case SCREEN_ID_SETTINGS:
        if (id == BTN_RIGHT) {
            if (!s_logout_confirm) {
                s_logout_confirm = true;
                ESP_LOGI(TAG, "logout: press RIGHT again to confirm");
            } else {
                ESP_LOGI(TAG, "logout confirmed");
                spotify_logout();
                s_logout_confirm = false;
                s_setup_locked = true;
                set_screen(SCREEN_ID_SETUP, "logged out");
            }
        } else if (id == BTN_LEFT) {
            if (s_logout_confirm) {
                ESP_LOGI(TAG, "logout cancelled");
            }
            s_logout_confirm = false;
        } else if (id == BTN_UP) {
            adjust_volume(CONFIG_PLAYER_VOLUME_STEP);
        } else if (id == BTN_DOWN) {
            adjust_volume(-(int)CONFIG_PLAYER_VOLUME_STEP);
        }
        break;

    case SCREEN_ID_PLAYER:
        if (id == BTN_LEFT) {
            ESP_LOGI(TAG, "previous track");
            player_prev();
        } else if (id == BTN_RIGHT) {
            ESP_LOGI(TAG, "next track");
            player_next();
        } else if (id == BTN_UP) {
            adjust_volume(CONFIG_PLAYER_VOLUME_STEP);
        } else if (id == BTN_DOWN) {
            adjust_volume(-(int)CONFIG_PLAYER_VOLUME_STEP);
        }
        break;
    }
}

static void fetch_library_page(void)
{
    size_t count = 0;
    int total = 0;
    char err[48] = {0};

    int offset = 0;
    if (xSemaphoreTake(s_data_lock, portMAX_DELAY) == pdTRUE) {
        s_lib_loading = true;
        s_lib_err[0] = '\0';
        offset = s_lib_offset < 0 ? 0 : s_lib_offset;
        xSemaphoreGive(s_data_lock);
    }
    ESP_LOGI(TAG, "library fetch start (offset=%d)", offset);

    if (media_client_configured()) {
        size_t n = 0;
        int media_total = 0;
        esp_err_t err_m = media_list_tracks(UI_PAGE_SIZE, offset, s_media_scratch,
                                            UI_PAGE_SIZE, &n, &media_total);
        if (err_m == ESP_OK) {
            total = media_total;
            for (size_t i = 0; i < n && count < UI_LIB_CAP; i++) {
                lib_item_t *it = &s_lib_scratch[count++];
                memset(it, 0, sizeof(*it));
                it->src = LIB_SRC_SERVER;
                strncpy(it->title, s_media_scratch[i].title, sizeof(it->title) - 1);
                strncpy(it->artist, s_media_scratch[i].artist, sizeof(it->artist) - 1);
                it->media_id = s_media_scratch[i].id;
                strncpy(it->encoding, s_media_scratch[i].encoding, sizeof(it->encoding) - 1);
                it->duration_s = s_media_scratch[i].duration_s;
            }
            ESP_LOGI(TAG, "library: %u server tracks (total %d)", (unsigned)n, media_total);
        } else {
            snprintf(err, sizeof(err), "Server error");
            ESP_LOGW(TAG, "library: media_list_tracks failed: %s", esp_err_to_name(err_m));
        }
    } else {
        ESP_LOGW(TAG, "library: media server not configured (PLAYER_MEDIA_SERVICE_URL)");
    }

    if (offset == 0) {
        esp_err_t sd_err = sd_storage_ensure_mounted();
        if (sd_err == ESP_OK) {
            size_t n = 0;
            int sd_total = 0;
            esp_err_t list_err = sd_storage_list_music(0, UI_PAGE_SIZE, s_sd_scratch,
                                                       UI_PAGE_SIZE, &n, &sd_total);
            if (list_err == ESP_OK) {
                total += sd_total;
                for (size_t i = 0; i < n && count < UI_LIB_CAP; i++) {
                    lib_item_t *it = &s_lib_scratch[count++];
                    memset(it, 0, sizeof(*it));
                    it->src = LIB_SRC_SD;
                    strncpy(it->title, s_sd_scratch[i].name, sizeof(it->title) - 1);
                    strncpy(it->path, s_sd_scratch[i].path, sizeof(it->path) - 1);
                    it->duration_s = -1;
                }
                ESP_LOGI(TAG, "library: %u SD files (total %d)", (unsigned)n, sd_total);
            } else {
                ESP_LOGW(TAG, "library: SD list failed: %s", esp_err_to_name(list_err));
            }
        } else {
            ESP_LOGW(TAG, "library: SD unavailable: %s", esp_err_to_name(sd_err));
            if (!media_client_configured()) {
                snprintf(err, sizeof(err), "No SD / server");
            }
        }
    }

    if (xSemaphoreTake(s_data_lock, portMAX_DELAY) == pdTRUE) {
        memcpy(s_lib, s_lib_scratch, sizeof(lib_item_t) * count);
        s_lib_count = count;
        s_lib_total = total;
        if (s_lib_sel >= (int)s_lib_count) {
            s_lib_sel = s_lib_count ? (int)s_lib_count - 1 : 0;
        }
        strncpy(s_lib_err, err, sizeof(s_lib_err) - 1);
        s_lib_loading = false;
        xSemaphoreGive(s_data_lock);
    }
    ESP_LOGI(TAG, "library fetch done: %u items, total %d%s%s", (unsigned)count, total,
             err[0] ? ", error: " : "", err);
}

static void fetch_import_page(void)
{
    size_t count = 0;
    int total = 0;
    char err[48] = {0};

    int offset = 0;
    if (xSemaphoreTake(s_data_lock, portMAX_DELAY) == pdTRUE) {
        s_imp_loading = true;
        s_imp_err[0] = '\0';
        offset = s_imp_offset < 0 ? 0 : s_imp_offset;
        xSemaphoreGive(s_data_lock);
    }
    ESP_LOGI(TAG, "import fetch start (offset=%d)", offset);

    auth_state_t *auth = auth_state_get();
    if (!auth->is_logged_in) {
        snprintf(err, sizeof(err), "Not logged in");
    } else {
        size_t n = 0;
        esp_err_t e = spotify_get_saved_tracks(UI_PAGE_SIZE, offset, NULL,
                                               s_spotify_scratch, UI_PAGE_SIZE, &n, &total);
        if (e == ESP_OK) {
            for (size_t i = 0; i < n && count < UI_IMPORT_CAP; i++) {
                import_item_t *it = &s_imp_scratch[count++];
                memset(it, 0, sizeof(*it));
                strncpy(it->title, s_spotify_scratch[i].track_name, sizeof(it->title) - 1);
                strncpy(it->artist, s_spotify_scratch[i].track_artist, sizeof(it->artist) - 1);
            }
        } else {
            snprintf(err, sizeof(err), "Spotify error");
            ESP_LOGW(TAG, "import: spotify_get_saved_tracks failed: %s", esp_err_to_name(e));
        }
    }

    if (xSemaphoreTake(s_data_lock, portMAX_DELAY) == pdTRUE) {
        memcpy(s_imp, s_imp_scratch, sizeof(import_item_t) * count);
        s_imp_count = count;
        s_imp_total = total;
        if (s_imp_sel >= (int)s_imp_count) {
            s_imp_sel = s_imp_count ? (int)s_imp_count - 1 : 0;
        }
        strncpy(s_imp_err, err, sizeof(s_imp_err) - 1);
        s_imp_loading = false;
        xSemaphoreGive(s_data_lock);
    }
    ESP_LOGI(TAG, "import fetch done: %u items, total %d%s%s", (unsigned)count, total,
             err[0] ? ", error: " : "", err);
}

static void fetch_task(void *arg)
{
    (void)arg;
    fetch_cmd_t cmd;
    while (1) {
        if (xQueueReceive(s_fetch_q, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (cmd == FETCH_LIBRARY) {
            fetch_library_page();
        } else if (cmd == FETCH_IMPORT) {
            fetch_import_page();
        }
        ESP_LOGD(TAG, "fetch task stack high-water: %u bytes",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

static void log_state_changes(void)
{
    transfer_state_t xfer = transfer_manager_state();
    if (xfer != s_last_xfer) {
        ESP_LOGI(TAG, "transfer %s -> %s%s%s", transfer_state_name(s_last_xfer),
                 transfer_state_name(xfer),
                 xfer == TRANSFER_DONE_FAILED ? ": " : "",
                 xfer == TRANSFER_DONE_FAILED ? transfer_manager_error() : "");
        s_last_xfer = xfer;
        if (xfer == TRANSFER_DONE_NEW || xfer == TRANSFER_DONE_DUPLICATE) {
            request_fetch(FETCH_LIBRARY);
        }
    }

    player_status_t st;
    player_get_status(&st);
    if (st.state != s_last_player_state || strncmp(st.title, s_last_player_title,
                                                   sizeof(s_last_player_title) - 1) != 0) {
        ESP_LOGI(TAG, "player %s -> %s '%s' (%u/%u)%s%s",
                 player_state_name(s_last_player_state), player_state_name(st.state),
                 st.title, (unsigned)(st.count ? st.index + 1 : 0), (unsigned)st.count,
                 st.state == PLAYER_ERROR ? " error: " : "",
                 st.state == PLAYER_ERROR ? player_error() : "");
        s_last_player_state = st.state;
        strncpy(s_last_player_title, st.title, sizeof(s_last_player_title) - 1);
        s_last_player_title[sizeof(s_last_player_title) - 1] = '\0';
    }
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    const char *reason = setup_lock_reason();
    bool locked = reason != NULL;
    if (locked != s_setup_locked) {
        s_setup_locked = locked;
        if (locked) {
            ESP_LOGW(TAG, "locked on Setup: %s", reason);
            if (s_screen != SCREEN_ID_SETUP) {
                ESP_LOGI(TAG, "tab %s -> Setup (%s)", screen_name(s_screen), reason);
            }
            s_screen = SCREEN_ID_SETUP;
            persist_screen();
        } else {
            ESP_LOGI(TAG, "Setup unlocked (Wi-Fi + Spotify OK)");
            if (s_screen == SCREEN_ID_SETUP) {
                set_screen(SCREEN_ID_LIBRARY, "setup complete");
            }
        }
    }

    button_event_t ev;
    while (buttons_pop_event(&ev, 0)) {
        handle_button(ev.id);
    }

    log_state_changes();
    ui_refresh_labels();
}

esp_err_t ui_init(void)
{
    lv_display_t *disp = get_disp();
    if (!disp) {
        return ESP_ERR_INVALID_STATE;
    }

    s_data_lock = xSemaphoreCreateMutex();
    s_fetch_q = xQueueCreate(1, sizeof(fetch_cmd_t));
    if (!s_data_lock || !s_fetch_q) {
        return ESP_ERR_NO_MEM;
    }

    display_lv_lock();

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_clean(scr);
    // Portrait 240x320: header, flexible body, footer. Black field, white text.
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    lv_obj_set_style_text_font(scr, ui_font_body(), 0);
    lv_obj_set_style_pad_all(scr, 6, 0);
    lv_obj_set_style_pad_row(scr, 4, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(scr, false);

    s_header = lv_label_create(scr);
    lv_obj_set_width(s_header, LV_PCT(100));
    lv_obj_set_style_text_font(s_header, ui_font_title(), 0);
    lv_label_set_long_mode(s_header, LV_LABEL_LONG_CLIP);

    // Body fills whatever the header and footer leave. Rows are 40 px so six
    // of them fit the remaining ~250 px without scrolling.
    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_row(body, 2, 0);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(body, false);

    s_text = lv_label_create(body);
    lv_obj_set_width(s_text, LV_PCT(100));
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);

    for (int i = 0; i < UI_PAGE_SIZE; i++) {
        lv_obj_t *row = lv_obj_create(body);
        s_rows[i] = row;
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 40);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_ACCENT_HEX), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_set_style_pad_ver(row, 2, 0);
        lv_obj_set_style_pad_row(row, 0, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_hidden(row, true);

        s_row_title[i] = lv_label_create(row);
        lv_obj_set_width(s_row_title[i], LV_PCT(100));
        lv_label_set_long_mode(s_row_title[i], LV_LABEL_LONG_CLIP);

        s_row_artist[i] = lv_label_create(row);
        lv_obj_set_width(s_row_artist[i], LV_PCT(100));
        lv_label_set_long_mode(s_row_artist[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(s_row_artist[i], lv_color_hex(0xB3B3B3), 0);
    }

    s_player = lv_obj_create(body);
    lv_obj_set_width(s_player, LV_PCT(100));
    lv_obj_set_flex_grow(s_player, 1);
    lv_obj_set_flex_flow(s_player, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(s_player, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_player, 0, 0);
    lv_obj_set_style_pad_all(s_player, 4, 0);
    lv_obj_set_style_pad_row(s_player, 8, 0);
    lv_obj_set_scrollbar_mode(s_player, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(s_player, false);
    lv_obj_set_hidden(s_player, true);

    s_play_title = lv_label_create(s_player);
    lv_obj_set_width(s_play_title, LV_PCT(100));
    lv_obj_set_style_text_font(s_play_title, ui_font_title(), 0);
    lv_label_set_long_mode(s_play_title, LV_LABEL_LONG_SCROLL_CIRCULAR);

    s_play_artist = lv_label_create(s_player);
    lv_obj_set_width(s_play_artist, LV_PCT(100));
    lv_label_set_long_mode(s_play_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(s_play_artist, lv_color_hex(0xB3B3B3), 0);

    s_play_bar = lv_bar_create(s_player);
    lv_obj_set_width(s_play_bar, LV_PCT(100));
    lv_obj_set_height(s_play_bar, 10);
    lv_bar_set_range(s_play_bar, 0, 100);
    lv_bar_set_value(s_play_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_play_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_play_bar, lv_color_hex(UI_ACCENT_HEX), LV_PART_INDICATOR);

    s_play_time = lv_label_create(s_player);
    lv_obj_set_width(s_play_time, LV_PCT(100));

    s_play_meta = lv_label_create(s_player);
    lv_obj_set_width(s_play_meta, LV_PCT(100));
    lv_obj_set_style_text_color(s_play_meta, lv_color_hex(0xB3B3B3), 0);

    s_status = lv_label_create(scr);
    lv_obj_set_width(s_status, LV_PCT(100));
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_SCROLL_CIRCULAR);

    const char *reason = setup_lock_reason();
    s_setup_locked = reason != NULL;
    if (s_setup_locked) {
        s_screen = SCREEN_ID_SETUP;
        ESP_LOGW(TAG, "locked on Setup: %s", reason);
    } else {
        enum ScreensEnum saved = device_state_get()->current_screen;
        if (saved > SCREEN_ID_PLAYER || saved == SCREEN_ID_SETUP) {
            saved = SCREEN_ID_LIBRARY;
        }
        s_screen = saved;
    }
    persist_screen();

    lv_timer_create(ui_timer_cb, UI_POLL_MS, NULL);
    ui_refresh_labels();

    display_lv_unlock();

    BaseType_t ok = xTaskCreate(fetch_task, "ui_fetch", UI_FETCH_STACK, NULL,
                                UI_FETCH_PRIO, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if (s_screen == SCREEN_ID_LIBRARY) {
        request_fetch(FETCH_LIBRARY);
    } else if (s_screen == SCREEN_ID_IMPORT) {
        request_fetch(FETCH_IMPORT);
    }

    ESP_LOGI(TAG, "UI ready on %s. Controls: C=next tab, hold C=play/pause, "
             "hold Up 5s=AUX melody, U/D=move/volume, R=action, L=back",
             screen_name(s_screen));
    return ESP_OK;
}

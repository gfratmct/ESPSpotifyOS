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
#include "services/player.h"
#include "services/transfer_manager.h"
#include "storage/sd_storage.h"

#define TAG "ui"

#define UI_PAGE_SIZE       4
// One server page plus one SD page on the first library page.
#define UI_LIB_CAP         (UI_PAGE_SIZE * 2)
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

static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_status;

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

static void render_list(char *body, size_t body_sz, const char *const *titles,
                        const char *kinds, size_t count, int sel)
{
    body[0] = '\0';
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        char line[48];
        const char mark = ((int)i == sel) ? '>' : ' ';
        if (kinds) {
            snprintf(line, sizeof(line), "%c%c %.13s\n", mark, kinds[i], titles[i]);
        } else {
            snprintf(line, sizeof(line), "%c %.14s\n", mark, titles[i]);
        }
        size_t len = strlen(line);
        if (used + len >= body_sz) {
            break;
        }
        memcpy(body + used, line, len + 1);
        used += len;
    }
}

static void render_library(char *body, size_t body_sz, char *status, size_t status_sz)
{
    if (xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        snprintf(body, body_sz, "...");
        status[0] = '\0';
        return;
    }

    if (s_lib_loading && s_lib_count == 0) {
        snprintf(body, body_sz, "Loading...");
        snprintf(status, status_sz, "C:next tab");
    } else if (s_lib_err[0] && s_lib_count == 0) {
        snprintf(body, body_sz, "%s", s_lib_err);
        snprintf(status, status_sz, "C:next tab");
    } else if (s_lib_count == 0) {
        snprintf(body, body_sz, "Empty library\nImport tracks\nor insert SD");
        snprintf(status, status_sz, "C:next tab");
    } else {
        const char *titles[UI_LIB_CAP];
        char kinds[UI_LIB_CAP];
        for (size_t i = 0; i < s_lib_count; i++) {
            titles[i] = s_lib[i].title;
            kinds[i] = (s_lib[i].src == LIB_SRC_SERVER) ? 'S' : 'D';
        }
        render_list(body, body_sz, titles, kinds, s_lib_count, s_lib_sel);
        snprintf(status, status_sz, "%d/%d R:play C:tab",
                 s_lib_offset + s_lib_sel + 1,
                 s_lib_total > 0 ? s_lib_total : (int)s_lib_count);
    }
    xSemaphoreGive(s_data_lock);
}

static void render_import(char *body, size_t body_sz, char *status, size_t status_sz)
{
    transfer_state_t xfer = transfer_manager_state();
    if (xfer == TRANSFER_RUNNING) {
        snprintf(body, body_sz, "Importing...\nPlease wait");
        snprintf(status, status_sz, "C:next tab");
        return;
    }
    if (xfer == TRANSFER_DONE_NEW || xfer == TRANSFER_DONE_DUPLICATE) {
        snprintf(body, body_sz, "Import done\n(%s)",
                 xfer == TRANSFER_DONE_NEW ? "new" : "already there");
        snprintf(status, status_sz, "R:ok C:tab");
        return;
    }
    if (xfer == TRANSFER_DONE_FAILED) {
        snprintf(body, body_sz, "Import failed\n%.40s", transfer_manager_error());
        snprintf(status, status_sz, "R:ok C:tab");
        return;
    }

    if (xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        snprintf(body, body_sz, "...");
        status[0] = '\0';
        return;
    }
    if (s_imp_loading && s_imp_count == 0) {
        snprintf(body, body_sz, "Loading liked...");
        snprintf(status, status_sz, "C:next tab");
    } else if (s_imp_err[0] && s_imp_count == 0) {
        snprintf(body, body_sz, "%s", s_imp_err);
        snprintf(status, status_sz, "C:next tab");
    } else if (s_imp_count == 0) {
        snprintf(body, body_sz, "No liked tracks");
        snprintf(status, status_sz, "C:next tab");
    } else {
        const char *titles[UI_IMPORT_CAP];
        for (size_t i = 0; i < s_imp_count; i++) {
            titles[i] = s_imp[i].title;
        }
        render_list(body, body_sz, titles, NULL, s_imp_count, s_imp_sel);
        snprintf(status, status_sz, "%d/%d R:get C:tab",
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
    snprintf(body, body_sz, "IP %s\nVol %u%% (U/D)\nR: logout",
             ip, (unsigned)player_get_volume());
    snprintf(status, status_sz, "WiFi: /settings");
}

static void render_player(char *body, size_t body_sz, char *status, size_t status_sz)
{
    player_status_t st;
    player_get_status(&st);

    if (st.count == 0) {
        snprintf(body, body_sz, "Nothing queued\nPick a track\nin Library");
        snprintf(status, status_sz, "C:next tab");
        return;
    }

    snprintf(body, body_sz, "%.16s\n%.16s\n%d/%ds vol%u%%",
             st.title[0] ? st.title : "-",
             st.artist[0] ? st.artist : "-",
             st.position_s, st.duration_s > 0 ? st.duration_s : 0,
             (unsigned)st.volume);
    snprintf(status, status_sz, "%s %u/%u  hold C",
             player_state_name(st.state),
             (unsigned)(st.index + 1), (unsigned)st.count);
}

static void ui_set_label_if_changed(lv_obj_t *label, const char *text)
{
    const char *cur = lv_label_get_text(label);
    if (cur && strcmp(cur, text) == 0) {
        return;
    }
    lv_label_set_text(label, text);
}

static void ui_refresh_labels(void)
{
    char body[160];
    char status[48];
    body[0] = '\0';
    status[0] = '\0';

    switch (s_screen) {
    case SCREEN_ID_SETUP:
        render_setup(body, sizeof(body), status, sizeof(status));
        break;
    case SCREEN_ID_LIBRARY:
        render_library(body, sizeof(body), status, sizeof(status));
        break;
    case SCREEN_ID_IMPORT:
        render_import(body, sizeof(body), status, sizeof(status));
        break;
    case SCREEN_ID_SETTINGS:
        render_settings(body, sizeof(body), status, sizeof(status));
        break;
    case SCREEN_ID_PLAYER:
        render_player(body, sizeof(body), status, sizeof(status));
        break;
    }

    char title[32];
    snprintf(title, sizeof(title), "%d/%d %s%s", tab_index(s_screen) + 1, UI_TAB_COUNT,
             screen_name(s_screen), s_setup_locked ? " (locked)" : "");
    ui_set_label_if_changed(s_title, title);
    ui_set_label_if_changed(s_body, body);
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

// Controls:
//   CENTER       next tab (everywhere)
//   CENTER_LONG  play/pause (everywhere)
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
    // OLED look: black background (pixels off), white text (pixels on).
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    s_title = lv_label_create(scr);
    lv_obj_set_width(s_title, DISPLAY_W);
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // 9 px unscii_8 lines: title y0-8, body y10-54 (5 lines), status y55-63.
    s_body = lv_label_create(scr);
    lv_obj_set_width(s_body, DISPLAY_W);
    lv_obj_set_height(s_body, 45);
    lv_obj_align(s_body, LV_ALIGN_TOP_LEFT, 0, 10);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_CLIP);

    s_status = lv_label_create(scr);
    lv_obj_set_width(s_status, DISPLAY_W);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, 0, 0);
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
             "U/D=move/volume, R=action, L=back", screen_name(s_screen));
    return ESP_OK;
}

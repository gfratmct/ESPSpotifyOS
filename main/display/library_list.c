#include "display/library_list.h"

#include <stdio.h>
#include <string.h>

#include <esp_log.h>

#include "fonts.h"

#include "services/transfer_manager.h"

#define TAG "library_list"

#define LIST_PAGE_SIZE 24
#define LIST_PANEL_WIDTH 200
#define LIST_ITEM_PADDING_HOR 6
#define LIST_ITEM_PADDING_VER 4
#define LIST_ITEM_MIN_HEIGHT 24

static lv_obj_t *s_container;
static lv_obj_t *s_status_label;
static const library_source_t *s_source;

static library_item_t s_items[LIST_PAGE_SIZE];
static size_t s_loaded_count;
static int s_total_items;
static int s_page_offset;
static bool s_page_loading;
static lv_obj_t *s_active_item;

// LVGL-task-only state for the in-flight transfer
static lv_obj_t *s_transfer_item;
static char s_transfer_item_original[160];
static char s_status_original[64];
static lv_timer_t *s_status_timer;

static void load_page(int offset, bool scroll_to_end);

static void clear_list(void)
{
    if (!s_container) return;
    lv_obj_clean(s_container);
    s_active_item = NULL;
    s_transfer_item = NULL;
}

static void item_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *item = lv_event_get_target(e);
    const library_item_t *entry = lv_event_get_user_data(e);

    if (code == LV_EVENT_CLICKED) {
        if (s_active_item && s_active_item != item) {
            lv_obj_clear_state(s_active_item, LV_STATE_CHECKED);
        }
        s_active_item = item;
        lv_obj_add_state(item, LV_STATE_CHECKED);
        if (entry) {
            ESP_LOGI(TAG, "Selected: %s - %s", entry->title, entry->subtitle);
        }
    }
}

// Restores the status label to what it showed before a transfer message.
static void status_revert_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer); // one-shot
    s_status_timer = NULL;
    if (s_status_label) {
        lv_label_set_text(s_status_label, s_status_original);
    }
}

// Sets the status label and, when revert_ms > 0, schedules it to go back to
// s_status_original. Cancels any pending revert first. LVGL task context only.
static void set_status(const char *text, uint32_t revert_ms)
{
    if (!s_status_label) return;
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    lv_label_set_text(s_status_label, text);
    if (revert_ms > 0) {
        s_status_timer = lv_timer_create(status_revert_cb, revert_ms, NULL);
    }
}

static void request_transfer(const library_item_t *entry, lv_obj_t *item)
{
    if (!entry || !s_source || !s_source->start_transfer) return;

    if (transfer_manager_state() == TRANSFER_RUNNING) {
        set_status("Already transferring...", 2000);
        return;
    }

    // remember the item + original label so we can mark it when done
    s_transfer_item = item;
    if (item) {
        lv_obj_t *label = lv_obj_get_child(item, 0);
        const char *text = label ? lv_label_get_text(label) : NULL;
        if (text) {
            strncpy(s_transfer_item_original, text, sizeof(s_transfer_item_original) - 1);
            s_transfer_item_original[sizeof(s_transfer_item_original) - 1] = '\0';
            lv_label_set_text_fmt(label, "%s ...", text);
        }
    }
    if (s_status_label) {
        const char *t = lv_label_get_text(s_status_label);
        strncpy(s_status_original, t ? t : "", sizeof(s_status_original) - 1);
        s_status_original[sizeof(s_status_original) - 1] = '\0';
    }

    transfer_request_result_t result = s_source->start_transfer(entry);
    if (result != TRANSFER_STARTED) {
        // undo the "..." we added to the item label
        if (s_transfer_item && lv_obj_is_valid(s_transfer_item)) {
            lv_obj_t *label = lv_obj_get_child(s_transfer_item, 0);
            if (label) {
                lv_label_set_text(label, s_transfer_item_original);
            }
        }
        s_transfer_item = NULL;
        set_status(result == TRANSFER_BUSY ? "Already transferring..." : "Transfer unavailable", 3000);
        return;
    }

    set_status("Importing...", 0); // replaced on completion
}

static void item_long_pressed_cb(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_target(e);
    const library_item_t *entry = lv_event_get_user_data(e);
    request_transfer(entry, item);
}

static int add_item(int y, const library_item_t *entry)
{
    lv_obj_t *item = lv_btn_create(s_container);
    lv_obj_set_pos(item, 0, y);
    lv_obj_set_width(item, LIST_PANEL_WIDTH);
    lv_obj_set_style_pad_hor(item, LIST_ITEM_PADDING_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(item, LIST_ITEM_PADDING_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(item, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(item, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(item, lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED | LV_STATE_CHECKED);

    lv_obj_t *label = lv_label_create(item);
    int label_width = LIST_PANEL_WIDTH - (2 * LIST_ITEM_PADDING_HOR);
    lv_obj_set_width(label, label_width);
    lv_obj_set_pos(label, LIST_ITEM_PADDING_HOR, LIST_ITEM_PADDING_VER);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    char text[288];
    snprintf(text, sizeof(text), "%s - %s", entry->title, entry->subtitle);
    lv_label_set_text(label, text);
    lv_obj_update_layout(label);
    int height = lv_obj_get_height(label) + (2 * LIST_ITEM_PADDING_VER);
    if (height < LIST_ITEM_MIN_HEIGHT) {
        height = LIST_ITEM_MIN_HEIGHT;
    }
    lv_obj_set_height(item, height);

    lv_obj_add_event_cb(item, item_event_handler, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_add_event_cb(item, item_long_pressed_cb, LV_EVENT_LONG_PRESSED, (void *)entry);
    return height;
}

static void add_message(const char *text)
{
    lv_obj_t *label = lv_label_create(s_container);
    lv_obj_set_size(label, LIST_PANEL_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(label, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(label, text);
}

static void scroll_event_handler(lv_event_t *e)
{
    lv_obj_t *container = lv_event_get_target(e);
    if (s_page_loading || s_loaded_count == 0) return;

    if (lv_obj_get_scroll_bottom(container) <= LIST_ITEM_MIN_HEIGHT &&
        s_page_offset + (int)s_loaded_count < s_total_items) {
        load_page(s_page_offset + (int)s_loaded_count, false);
    } else if (lv_obj_get_scroll_top(container) <= LIST_ITEM_MIN_HEIGHT && s_page_offset > 0) {
        int previous = s_page_offset - LIST_PAGE_SIZE;
        load_page(previous < 0 ? 0 : previous, true);
    }
}

static void load_page(int offset, bool scroll_to_end)
{
    if (!s_container || !s_source || s_page_loading) return;

    s_page_loading = true;
    size_t count = 0;
    int total = 0;
    esp_err_t err = s_source->fetch(offset, LIST_PAGE_SIZE, s_items, LIST_PAGE_SIZE, &count, &total);

    clear_list();
    s_loaded_count = 0;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch '%s' page: %s", s_source->name, esp_err_to_name(err));
        add_message("Failed to load");
    } else if (count == 0) {
        add_message(offset == 0 ? s_source->empty_message : "No more items");
    } else {
        s_page_offset = offset;
        s_total_items = total;
        s_loaded_count = count;
        int y = 0;
        for (size_t i = 0; i < count; i++) {
            y += add_item(y, &s_items[i]);
        }
        ESP_LOGI(TAG, "%s: loaded items %d-%d of %d", s_source->name,
                 offset + 1, offset + (int)count, total);
    }

    lv_obj_update_layout(s_container);
    lv_obj_scroll_to_y(s_container, scroll_to_end ? LV_COORD_MAX : 0, LV_ANIM_OFF);

    s_page_loading = false;
}

void library_list_attach(lv_obj_t *container, lv_obj_t *status_label)
{
    s_container = container;
    s_status_label = status_label;
}

void library_list_set_source(const library_source_t *source)
{
    s_source = source;
    s_page_offset = 0;
    s_total_items = 0;
}

void library_list_refresh(void)
{
    if (!s_container) {
        ESP_LOGW(TAG, "List not attached yet, skipping refresh");
        return;
    }
    if (!s_source) {
        ESP_LOGW(TAG, "No source selected, skipping refresh");
        return;
    }

    s_page_offset = 0;
    s_total_items = 0;
    load_page(0, false);

    lv_obj_remove_event_cb_with_user_data(s_container, scroll_event_handler, NULL);
    lv_obj_add_event_cb(s_container, scroll_event_handler, LV_EVENT_SCROLL_END, NULL);
}

void library_list_service_transfer(void)
{
    transfer_state_t state = transfer_manager_state();
    switch (state) {
    case TRANSFER_DONE_NEW:
    case TRANSFER_DONE_DUPLICATE:
    case TRANSFER_DONE_FAILED:
        break;
    default:
        return;
    }

    // mark the affected item in the list (it may have been paged away meanwhile)
    if (s_transfer_item && lv_obj_is_valid(s_transfer_item)) {
        lv_obj_t *label = lv_obj_get_child(s_transfer_item, 0);
        if (label) {
            const char *suffix = (state == TRANSFER_DONE_NEW) ? "[ok]"
                                : (state == TRANSFER_DONE_DUPLICATE) ? "[dup]" : "[fail]";
            lv_label_set_text_fmt(label, "%s %s", s_transfer_item_original, suffix);
        }
    }
    s_transfer_item = NULL;

    const char *msg = (state == TRANSFER_DONE_NEW) ? "Imported"
                    : (state == TRANSFER_DONE_DUPLICATE) ? "Already in library" : "Transfer failed";
    if (state == TRANSFER_DONE_FAILED && transfer_manager_error()[0]) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s (%s)", msg, transfer_manager_error());
        set_status(buf, 4000);
    } else {
        set_status(msg, 4000);
    }
    ESP_LOGI(TAG, "%s", msg);

    transfer_manager_clear();
}

#include "library_screen.h"

#include <stdint.h>

#include <esp_log.h>
#include "lvgl.h"

#include "fonts.h"
#include "screens.h"

#include "display/library_list.h"
#include "display/library_source.h"

#define TAG "library_screen"

#define TAB_COUNT 3
#define TAB_WIDTH 64
#define TAB_HEIGHT 34
#define TAB_SPACING 68
#define TAB_Y 240
#define LIST_HEIGHT 200

static const library_source_t *s_sources[TAB_COUNT];
static lv_obj_t *s_tab_buttons[TAB_COUNT];

// set from any task; consumed by the ui_state loop in the LVGL task
static volatile bool s_refresh_requested = false;

static void select_tab(int index);

static void tab_event_cb(lv_event_t *e)
{
    select_tab((int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *create_tab(lv_obj_t *parent, const char *text, int index)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, TAB_WIDTH, TAB_HEIGHT);
    lv_obj_set_pos(btn, index * TAB_SPACING, TAB_Y);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_CHECKED);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &font_dm_sans_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, tab_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    return btn;
}

static void apply_tab_states(int selected)
{
    for (int i = 0; i < TAB_COUNT; i++) {
        if (!s_tab_buttons[i]) continue;
        if (i == selected) {
            lv_obj_add_state(s_tab_buttons[i], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_tab_buttons[i], LV_STATE_CHECKED);
        }
    }
}

static void select_tab(int index)
{
    if (index < 0 || index >= TAB_COUNT || !s_sources[index]) return;

    apply_tab_states(index);
    ESP_LOGI(TAG, "Selected tab: %s", s_sources[index]->name);
    library_list_set_source(s_sources[index]);
    library_list_refresh();
}

void library_screen_init(void)
{
    s_sources[0] = library_source_spotify();
    s_sources[1] = library_source_server();
    s_sources[2] = library_source_sd();

    // give the list room for the tab bar at the bottom of the panel
    if (objects.home_library_container) {
        lv_obj_set_height(objects.home_library_container, LIST_HEIGHT);
    }

    lv_obj_t *parent = objects.panel_1 ? objects.panel_1 : objects.home;
    if (parent) {
        for (int i = 0; i < TAB_COUNT; i++) {
            s_tab_buttons[i] = create_tab(parent, s_sources[i]->name, i);
        }
    }

    library_list_attach(objects.home_library_container, objects.home_label_top);

    // select the Spotify tab without fetching: there is no network yet
    apply_tab_states(0);
    library_list_set_source(s_sources[0]);
}

void library_screen_refresh(void)
{
    library_list_refresh();
}

void library_screen_service_transfer(void)
{
    library_list_service_transfer();
}

void library_screen_request_refresh(void)
{
    // consumed by the ui_state loop in the LVGL task — safe from any task
    s_refresh_requested = true;
}

bool library_screen_take_refresh_request(void)
{
    bool requested = s_refresh_requested;
    s_refresh_requested = false;
    return requested; // must only be called from the LVGL task context
}

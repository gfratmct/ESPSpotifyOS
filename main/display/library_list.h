#pragma once

#include "lvgl.h"

#include "display/library_source.h"

#ifdef __cplusplus
extern "C" {
#endif

// Binds the list widget to a scroll container and a status label. Both are
// borrowed (owned by the screen) and may be NULL until the screen exists.
void library_list_attach(lv_obj_t *container, lv_obj_t *status_label);

// Sets the data source. Does not fetch; call library_list_refresh() for that.
void library_list_set_source(const library_source_t *source);

// Reloads the first page of the current source. LVGL task context only.
void library_list_refresh(void);

// Applies a finished background transfer to the UI. LVGL task context only.
void library_list_service_transfer(void);

#ifdef __cplusplus
}
#endif

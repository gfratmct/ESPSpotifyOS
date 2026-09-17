#include "display/setup_screen.h"

#include <stdio.h>

#include "lvgl.h"
#include "screens.h"

void setup_screen_show_status(const char *line1, const char *line2, const char *line3)
{
    if (!objects.status) {
        return;
    }
    // widen + center the label (generator default is a narrow left-placed one)
    lv_obj_set_pos(objects.status, 0, 196);
    lv_obj_set_size(objects.status, 200, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(objects.status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    const char *lines[3] = { line1, line2, line3 };
    char text[128];
    size_t off = 0;
    for (int i = 0; i < 3 && lines[i]; i++) {
        int written = snprintf(text + off, sizeof(text) - off, i ? "\n%s" : "%s", lines[i]);
        if (written < 0 || (size_t)written >= sizeof(text) - off) {
            break;
        }
        off += (size_t)written;
    }
    lv_label_set_text(objects.status, text);
}

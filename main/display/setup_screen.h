#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Shows a message on the setup screen's status label, up to three centered
// lines (pass NULL to stop early). Safe to call before the LVGL task starts.
void setup_screen_show_status(const char *line1, const char *line2, const char *line3);

#ifdef __cplusplus
}
#endif

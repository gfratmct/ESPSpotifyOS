#include "server/routes.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "connectivity/wifi.h"
#include "data/device_state.h"
#include "display/library_screen.h"
#include "net/spotify_auth.h"

#define TAG "routes"

#define SUBMIT_PATH      "/submit"
#define SUBMIT_MAX_BODY  2048
#define OAUTH_URL_MAX    1024
#define OAUTH_PLACEHOLDER "{{SPOTIFY_OAUTH_URL}}"

#define SETTINGS_PATH        "/settings"
#define SETTINGS_RESET_PATH  "/settings/reset"
#define WIFI_SCAN_PATH       "/wifi-scan"
#define SETTINGS_MAX_BODY    1024
#define WIFI_SSID_PLACEHOLDER "{{WIFI_SSID}}"
#define WIFI_SCAN_MAX_APS    20
#define RESTART_DELAY_MS     1500

extern const uint8_t submit_html_start[]   asm("_binary_submit_html_start");
extern const uint8_t submit_html_end[]     asm("_binary_submit_html_end");
extern const uint8_t settings_html_start[] asm("_binary_settings_html_start");
extern const uint8_t settings_html_end[]   asm("_binary_settings_html_end");

// ---- helpers ----------------------------------------------------------------

// Returns a heap-allocated copy of `tpl` with the first occurrence of
// `placeholder` replaced by `value`. Returns NULL when the placeholder is
// absent (the caller then serves the template unchanged) or on allocation
// failure. Caller frees the result.
static char *render_template(const char *tpl, size_t tpl_len,
                             const char *placeholder, const char *value) {
    const char *pos = strstr(tpl, placeholder);
    if (!pos) return NULL;

    size_t before = (size_t)(pos - tpl);
    size_t ph_len = strlen(placeholder);
    size_t val_len = strlen(value);
    size_t after = tpl_len - before - ph_len;
    size_t total = before + val_len + after;

    char *out = malloc(total + 1);
    if (!out) return NULL;

    memcpy(out, tpl, before);
    memcpy(out + before, value, val_len);
    memcpy(out + before + val_len, pos + ph_len, after);
    out[total] = '\0';
    return out;
}

// Decodes a URL-encoded string in place ("+" -> space, "%xx" -> byte).
static void url_decode(char *s) {
    char *out = s;
    for (char *in = s; *in; in++) {
        if (*in == '+') {
            *out++ = ' ';
        } else if (*in == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2])) {
            char hex[3] = { in[1], in[2], '\0' };
            *out++ = (char)strtol(hex, NULL, 16);
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = '\0';
}

// Parses an "ssid=..&password=.." form body in place into the two fields.
// Body is modified (separators become NUL); values are URL-decoded. Either
// output is NULL when the field is absent.
static void parse_settings_form(char *body, char **out_ssid, char **out_password) {
    *out_ssid = NULL;
    *out_password = NULL;

    char *p = body;
    while (p && *p) {
        char *amp = strchr(p, '&');
        if (amp) *amp = '\0';

        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *k = p;
            char *v = eq + 1;
            url_decode(k);
            url_decode(v);
            if (strcmp(k, "ssid") == 0) {
                *out_ssid = v;
            } else if (strcmp(k, "password") == 0) {
                *out_password = v;
            }
        }
        p = amp ? amp + 1 : NULL;
    }
}

// Restarts the device shortly after the HTTP response has been flushed, so the
// browser gets the confirmation page before the connection drops.
static esp_timer_handle_t s_restart_timer;
static bool s_restart_pending;

static void restart_cb(void *arg) {
    (void)arg;
    esp_restart();
}

static void schedule_restart(void) {
    if (s_restart_pending) return;
    s_restart_pending = true;

    const esp_timer_create_args_t args = { .callback = restart_cb, .name = "restart" };
    if (esp_timer_create(&args, &s_restart_timer) == ESP_OK) {
        esp_timer_start_once(s_restart_timer, RESTART_DELAY_MS * 1000);
    } else {
        esp_restart();
    }
}

// ---- /submit (Spotify OAuth) -------------------------------------------------

// GET /submit — serves the login page with the Spotify authorize URL injected.
static esp_err_t handle_submit_get(httpd_req_t *req) {
    char oauth_url[OAUTH_URL_MAX];
    if (spotify_get_authorize_url(oauth_url, sizeof(oauth_url)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build Spotify OAuth URL");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    const char *tpl = (const char *)submit_html_start;
    size_t tpl_len = (size_t)(submit_html_end - submit_html_start);

    char *html = render_template(tpl, tpl_len, OAUTH_PLACEHOLDER, oauth_url);
    const char *body = html ? html : tpl;
    size_t body_len = html ? strlen(html) : tpl_len;

    httpd_resp_set_type(req, "text/html");
    int ret = httpd_resp_send(req, body, body_len);
    free(html);
    return ret;
}

// POST /submit — accepts "spotify_code=<code>", exchanges it for tokens.
static esp_err_t handle_submit_post(httpd_req_t *req) {
    if (req->content_len == 0 || req->content_len > SUBMIT_MAX_BODY) {
        ESP_LOGE(TAG, "Rejecting submit body of size %u", (unsigned)req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_FAIL;
    }

    char *body = malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) {
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        free(body);
        return ESP_FAIL;
    }
    body[received] = '\0';

    static const char prefix[] = "spotify_code=";
    char *code = (strncmp(body, prefix, sizeof(prefix) - 1) == 0) ? body + sizeof(prefix) - 1 : NULL;
    if (!code || !*code) {
        ESP_LOGE(TAG, "Missing spotify_code in submit body");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing spotify_code");
        free(body);
        return ESP_FAIL;
    }

    // `code` points into body, so exchange before freeing it
    esp_err_t err = spotify_exchange_code_for_token(code);
    free(body);

    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // repopulate the library (runs inside the LVGL task via the ui_state loop)
    library_screen_request_refresh();

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ---- /settings (Wi-Fi provisioning) ------------------------------------------

// GET /settings — serves the Wi-Fi settings page with the current SSID filled.
static esp_err_t handle_settings_get(httpd_req_t *req) {
    device_state_t *device = device_state_get();
    const char *ssid = device ? (const char *)device->wifi_ssid : "";

    const char *tpl = (const char *)settings_html_start;
    size_t tpl_len = (size_t)(settings_html_end - settings_html_start);

    char *html = render_template(tpl, tpl_len, WIFI_SSID_PLACEHOLDER, ssid);
    const char *body = html ? html : tpl;
    size_t body_len = html ? strlen(html) : tpl_len;

    httpd_resp_set_type(req, "text/html");
    int ret = httpd_resp_send(req, body, body_len);
    free(html);
    return ret;
}

// POST /settings — saves new Wi-Fi credentials and restarts to apply them.
static esp_err_t handle_settings_post(httpd_req_t *req) {
    if (req->content_len == 0 || req->content_len > SETTINGS_MAX_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_FAIL;
    }

    char *body = malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) {
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        free(body);
        return ESP_FAIL;
    }
    body[received] = '\0';

    char *ssid = NULL;
    char *password = NULL;
    parse_settings_form(body, &ssid, &password);

    if (!ssid || ssid[0] == '\0' || strlen(ssid) > 31) {
        ESP_LOGE(TAG, "Invalid SSID");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID must be 1-31 characters");
        free(body);
        return ESP_FAIL;
    }
    if (password && strlen(password) > 63) {
        ESP_LOGE(TAG, "Invalid password length");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password too long");
        free(body);
        return ESP_FAIL;
    }

    device_state_t *device = device_state_get();
    memset(device->wifi_ssid, 0, sizeof(device->wifi_ssid));
    memset(device->wifi_password, 0, sizeof(device->wifi_password));
    strncpy((char *)device->wifi_ssid, ssid, sizeof(device->wifi_ssid) - 1);
    if (password) {
        strncpy((char *)device->wifi_password, password, sizeof(device->wifi_password) - 1);
    }
    free(body);

    esp_err_t err = device_state_save();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Wi-Fi settings: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Wi-Fi settings saved, restarting");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<!DOCTYPE html><html><body style=\"font-family:Arial;background:#121212;color:#fff;"
        "text-align:center;padding:40px\"><h2 style=\"color:#1DB954\">Saved</h2>"
        "<p>The device is restarting and will connect to the new network.</p></body></html>",
        HTTPD_RESP_USE_STRLEN);

    schedule_restart();
    return ESP_OK;
}

// POST /settings/reset — restores build-time Wi-Fi defaults and restarts.
static esp_err_t handle_settings_reset_post(httpd_req_t *req) {
    ESP_LOGW(TAG, "Wi-Fi settings reset to defaults, restarting");
    device_state_reset_wifi();

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<!DOCTYPE html><html><body style=\"font-family:Arial;background:#121212;color:#fff;"
        "text-align:center;padding:40px\"><h2 style=\"color:#1DB954\">Reset</h2>"
        "<p>Wi-Fi settings were restored to defaults. The device is restarting.</p></body></html>",
        HTTPD_RESP_USE_STRLEN);

    schedule_restart();
    return ESP_OK;
}

// GET /wifi-scan — JSON array of nearby networks (provisioning mode only).
static esp_err_t handle_wifi_scan_get(httpd_req_t *req) {
    static wifi_ap_info_t aps[WIFI_SCAN_MAX_APS];
    int found = wifi_scan(aps, WIFI_SCAN_MAX_APS);

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    for (int i = 0; i < found; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", aps[i].rssi);
        cJSON_AddBoolToObject(ap, "secure", aps[i].secure);
        cJSON_AddItemToArray(arr, ap);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    int ret = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ret;
}

esp_err_t routes_register(webserver_t *webserver) {
    esp_err_t err = webserver_register_route(webserver, HTTP_GET, SUBMIT_PATH, handle_submit_get);
    if (err != ESP_OK) return err;
    err = webserver_register_route(webserver, HTTP_POST, SUBMIT_PATH, handle_submit_post);
    if (err != ESP_OK) return err;
    err = webserver_register_route(webserver, HTTP_GET, SETTINGS_PATH, handle_settings_get);
    if (err != ESP_OK) return err;
    err = webserver_register_route(webserver, HTTP_POST, SETTINGS_PATH, handle_settings_post);
    if (err != ESP_OK) return err;
    err = webserver_register_route(webserver, HTTP_POST, SETTINGS_RESET_PATH, handle_settings_reset_post);
    if (err != ESP_OK) return err;
    return webserver_register_route(webserver, HTTP_GET, WIFI_SCAN_PATH, handle_wifi_scan_get);
}

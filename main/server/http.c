#include "http.h"

#include <esp_log.h>
#include <esp_system.h>

#include "utils/http.h"
#include "utils/spotify.h"
#include "display/home_ui.h"

#define TAG "http_server"

static webserver_t *s_webserver_instance = NULL;

extern const uint8_t submit_html_start[] asm("_binary_submit_html_start");
extern const uint8_t submit_html_end[]   asm("_binary_submit_html_end");

// routes

static esp_err_t handle_submit(httpd_req_t *req)
{
    // write a simple hello world for now
    httpd_resp_set_type(req, "text/html");

    // build spotify oauth login url
    char spotify_oauth_url[1024]; // buffer to store the Spotify OAuth URL
    if (spotify_get_authorize_url(spotify_oauth_url, sizeof(spotify_oauth_url)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build Spotify OAuth URL");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Spotify OAuth URL: %s", spotify_oauth_url);

    // inject the Spotify OAuth URL into the HTML
    // replace the {{SPOTIFY_OAUTH_URL}} placeholder in the HTML with the actual URL
    char *html_content = (char *)submit_html_start;
    size_t html_length = submit_html_end - submit_html_start;
    char *placeholder = "{{SPOTIFY_OAUTH_URL}}";
    char *pos = strstr(html_content, placeholder);
    if (pos) {
        size_t before_length = pos - html_content;
        size_t after_length = html_length - before_length - strlen(placeholder);
        char *new_html_content = (char *)malloc(before_length + strlen(spotify_oauth_url) + after_length + 1);
        if (new_html_content) {
            memcpy(new_html_content, html_content, before_length);
            memcpy(new_html_content + before_length, spotify_oauth_url, strlen(spotify_oauth_url));
            memcpy(new_html_content + before_length + strlen(spotify_oauth_url), pos + strlen(placeholder), after_length);
            new_html_content[before_length + strlen(spotify_oauth_url) + after_length] = '\0';
            html_content = new_html_content;
            html_length = before_length + strlen(spotify_oauth_url) + after_length;
        }
    }
    int ret = httpd_resp_send(req, html_content, html_length);
    if (html_content != (char *)submit_html_start) {
        free(html_content);
    }
    return ret;
}

static esp_err_t handle_post_submit(httpd_req_t *req)
{
    // handle the POST request for the Spotify code submission
    if (req->content_len == 0 || req->content_len > 2048) {
        ESP_LOGE(TAG, "Rejecting submit body of size %u", (unsigned)req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Handling POST submit with content length: %u", (unsigned)req->content_len);

    char *body = malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    memset(body, 0, req->content_len + 1);
    int ret = httpd_req_recv(req, body, req->content_len);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        free(body);
        return ESP_FAIL;
    }
    body[ret] = '\0';
    ESP_LOGI(TAG, "Received submit body: %s", body);

    // body: spotify_code=<code>
    // remove the "spotify_code=" prefix to extract the actual code
    char *spotify_code = NULL;
    const char *prefix = "spotify_code=";
    if (strncmp(body, prefix, strlen(prefix)) == 0) {
        spotify_code = body + strlen(prefix);
    }

    if (!spotify_code || !*spotify_code) {
        ESP_LOGE(TAG, "Missing spotify_code in submit body");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing spotify_code");
        free(body);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Extracted spotify_code: %s", spotify_code);

    // spotify_code points into body, so exchange before freeing it
    esp_err_t err = spotify_exchange_code_for_token(spotify_code);
    free(body);

    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // populate the Home screen with liked tracks (runs in the LVGL task)
    home_ui_request_refresh();

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

webserver_t *webserver_get_instance(void) {
    return s_webserver_instance;
}

webserver_t *webserver_create(int port) {
    if (s_webserver_instance != NULL) {
        ESP_LOGW(TAG, "Webserver instance already created");
        return s_webserver_instance;
    }

    s_webserver_instance = (webserver_t *)calloc(1, sizeof(webserver_t));
    if (!s_webserver_instance) {
        ESP_LOGE(TAG, "Failed to allocate webserver instance");
        return NULL;
    }

    s_webserver_instance->port = (port > 0) ? port : 80;
    s_webserver_instance->server = NULL;
    s_webserver_instance->is_running = false;
    strncpy(s_webserver_instance->root_path, "/", sizeof(s_webserver_instance->root_path) - 1);

    return s_webserver_instance;
}

esp_err_t webserver_start(webserver_t *webserver) {
    if (!webserver) {
        ESP_LOGE(TAG, "Cannot start NULL webserver");
        return ESP_ERR_INVALID_ARG;
    }

    if (webserver->is_running) {
        ESP_LOGW(TAG, "Webserver already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = (webserver->port > 0) ? webserver->port : 80;
    config.lru_purge_enable = true;
    config.stack_size = 16384; // Increase stack size to handle larger requests and responses

    ESP_LOGI(TAG, "Starting webserver on port %d", config.server_port);
    esp_err_t err = httpd_start(&webserver->server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver: %s (0x%x)", esp_err_to_name(err), err);
        return err;
    }

    // store routes
    route_get_path(webserver, "/submit", handle_submit);
    route_post_path(webserver, "/submit", handle_post_submit);

    webserver->is_running = true;
    s_webserver_instance = webserver;

    ESP_LOGI(TAG, "Webserver started successfully");
    return ESP_OK;
}

void webserver_stop(webserver_t *webserver) {
    if (!webserver || !webserver->is_running) {
        ESP_LOGW(TAG, "Webserver not running");
        return;
    }

    if (webserver->server != NULL) {
        httpd_stop(webserver->server);
        webserver->server = NULL;
    }
    webserver->is_running = false;
    ESP_LOGI(TAG, "Webserver stopped");
}

void webserver_destroy(webserver_t *webserver) {
    if (!webserver) {
        return;
    }

    webserver_stop(webserver);

    if (s_webserver_instance == webserver) {
        s_webserver_instance = NULL;
    }

    free(webserver);
    ESP_LOGI(TAG, "Webserver destroyed");
}

esp_err_t route_get_path(webserver_t *webserver, const char *path, esp_err_t (*handler)(httpd_req_t *r)) {
    if (!webserver || !webserver->server) return ESP_ERR_INVALID_STATE;
    httpd_uri_t uri_get = {
        .uri = path,
        .method = HTTP_GET,
        .handler = handler,
        .user_ctx = webserver
    };
    return httpd_register_uri_handler(webserver->server, &uri_get);
}

esp_err_t route_post_path(webserver_t *webserver, const char *path, esp_err_t (*handler)(httpd_req_t *r)) {
    if (!webserver || !webserver->server) return ESP_ERR_INVALID_STATE;
    httpd_uri_t uri_post = {
        .uri = path,
        .method = HTTP_POST,
        .handler = handler,
        .user_ctx = webserver
    };
    return httpd_register_uri_handler(webserver->server, &uri_post);
}

esp_err_t route_put_path(webserver_t *webserver, const char *path, esp_err_t (*handler)(httpd_req_t *r)) {
    if (!webserver || !webserver->server) return ESP_ERR_INVALID_STATE;
    httpd_uri_t uri_put = {
        .uri = path,
        .method = HTTP_PUT,
        .handler = handler,
        .user_ctx = webserver
    };
    return httpd_register_uri_handler(webserver->server, &uri_put);
}

esp_err_t route_delete_path(webserver_t *webserver, const char *path, esp_err_t (*handler)(httpd_req_t *r)) {
    if (!webserver || !webserver->server) return ESP_ERR_INVALID_STATE;
    httpd_uri_t uri_del = {
        .uri = path,
        .method = HTTP_DELETE,
        .handler = handler,
        .user_ctx = webserver
    };
    return httpd_register_uri_handler(webserver->server, &uri_del);
}
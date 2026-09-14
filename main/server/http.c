#include "http.h"

#include <esp_log.h>
#include <esp_system.h>

#define TAG "http_server"

static webserver_t *s_webserver_instance = NULL;

extern const uint8_t submit_html_start[] asm("_binary_submit_html_start");
extern const uint8_t submit_html_end[]   asm("_binary_submit_html_end");


// routes

static esp_err_t handle_submit(httpd_req_t *req)
{
    // write a simple hello world for now
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)submit_html_start,
                        submit_html_end - submit_html_start);
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

    ESP_LOGI(TAG, "Starting webserver on port %d", config.server_port);
    esp_err_t err = httpd_start(&webserver->server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver: %s (0x%x)", esp_err_to_name(err), err);
        return err;
    }

    // store routes
    route_get_path(webserver, "/submit", handle_submit);

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
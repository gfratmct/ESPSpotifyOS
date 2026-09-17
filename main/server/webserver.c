#include "server/webserver.h"

#include <stdlib.h>

#include <esp_log.h>

#include "server/routes.h"

#define TAG "webserver"

static webserver_t *s_webserver_instance = NULL;

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
    config.stack_size = 16384; // larger stack for the HTML templates + TLS-free responses

    ESP_LOGI(TAG, "Starting webserver on port %d", config.server_port);
    esp_err_t err = httpd_start(&webserver->server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver: %s (0x%x)", esp_err_to_name(err), err);
        return err;
    }

    webserver->is_running = true;
    s_webserver_instance = webserver;

    err = routes_register(webserver);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register routes: %s", esp_err_to_name(err));
        webserver_stop(webserver);
        return err;
    }

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

esp_err_t webserver_register_route(webserver_t *webserver, httpd_method_t method,
                                   const char *path, esp_err_t (*handler)(httpd_req_t *r)) {
    if (!webserver || !webserver->server || !path || !handler) return ESP_ERR_INVALID_ARG;

    httpd_uri_t uri = {
        .uri = path,
        .method = method,
        .handler = handler,
        .user_ctx = webserver,
    };
    return httpd_register_uri_handler(webserver->server, &uri);
}

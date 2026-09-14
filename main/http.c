#include "http.h"

#define TAG "HTTP"

static webserver_t *s_webserver_instance = NULL;

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

static esp_err_t http_client_event_handler(esp_http_client_event_t *evt) {
    http_response_t *resp = (http_response_t *)evt->user_data;
    if (!resp) return ESP_OK;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER: {
            // Append "Header-Key: Header-Value\n" to headers buffer
            size_t current_len = strlen(resp->headers);
            if (current_len < sizeof(resp->headers) - 1) {
                snprintf(resp->headers + current_len, 
                         sizeof(resp->headers) - current_len, 
                         "%s: %s\n", evt->header_key, evt->header_value);
            }
            break;
        }
        case HTTP_EVENT_ON_DATA: {
            // Append incoming chunk to body
            if (resp->data_len + evt->data_len < sizeof(resp->data)) {
                memcpy(resp->data + resp->data_len, evt->data, evt->data_len);
                resp->data_len += evt->data_len;
                resp->data[resp->data_len] = '\0';
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t http_get(const char *url, http_response_t *resp) {
    if (!url || !resp) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(resp, 0, sizeof(http_response_t));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_client_event_handler,
        .user_data = resp,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        resp->status_code = esp_http_client_get_status_code(client);
        resp->content_length = esp_http_client_get_content_length(client);
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t http_post(const char *url, const char *post_data, const char *content_type, http_response_t *resp) {
    if (!url || !resp) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(resp, 0, sizeof(http_response_t));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_client_event_handler,
        .user_data = resp,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    if (content_type) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }
    if (post_data) {
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        resp->status_code = esp_http_client_get_status_code(client);
        resp->content_length = esp_http_client_get_content_length(client);
    }

    esp_http_client_cleanup(client);
    return err;
}
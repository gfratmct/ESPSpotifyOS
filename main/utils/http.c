#include "http.h"

#define TAG "http_client"

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

    ESP_LOGI(TAG, "HTTP GET request to %s completed with status %d", url, resp->status_code);
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

    ESP_LOGI(TAG, "HTTP POST request to %s completed with status %d", url, resp->status_code);
    return err;
}
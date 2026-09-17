#include "net/http_client.h"
#include "esp_crt_bundle.h"

#define TAG "http_client"

#define HTTP_STREAM_BUF_SIZE 2048

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
            } else if (evt->data_len > 0) {
                // body larger than the buffer — flag it so callers can report
                // truncation instead of parsing a silently cut-off payload
                resp->truncated = true;
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t http_get(const char *url, const http_header_t *headers, size_t headers_count, http_response_t *resp) {
    return http_get_with_timeout(url, headers, headers_count, 5000, resp);
}

esp_err_t http_post(const char *url, const char *post_data, const char *content_type,
                     const http_header_t *headers, size_t headers_count, http_response_t *resp) {
    return http_post_with_timeout(url, post_data, content_type, headers, headers_count, 5000, resp);
}

esp_err_t http_get_with_timeout(const char *url, const http_header_t *headers, size_t headers_count,
                                 int timeout_ms, http_response_t *resp) {
    if (!url || !resp) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(resp, 0, sizeof(http_response_t));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_client_event_handler,
        .user_data = resp,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    for (size_t i = 0; i < headers_count; i++) {
        esp_http_client_set_header(client, headers[i].key, headers[i].value);
    }

    esp_err_t err = esp_http_client_perform(client);
    resp->status_code = esp_http_client_get_status_code(client);
    resp->content_length = esp_http_client_get_content_length(client);
    esp_http_client_cleanup(client);

    if (resp->truncated) {
        ESP_LOGW(TAG, "GET %s: body exceeded %u bytes and was truncated",
                 url, (unsigned)sizeof(resp->data));
    }
    ESP_LOGI(TAG, "HTTP GET request to %s completed with status %d", url, resp->status_code);
    return err;
}

esp_err_t http_post_with_timeout(const char *url, const char *post_data, const char *content_type,
                                  const http_header_t *headers, size_t headers_count,
                                  int timeout_ms, http_response_t *resp) {
    if (!url || !resp) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(resp, 0, sizeof(http_response_t));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_client_event_handler,
        .user_data = resp,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    if (content_type) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }
    for (size_t i = 0; i < headers_count; i++) {
        esp_http_client_set_header(client, headers[i].key, headers[i].value);
    }
    if (post_data) {
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
    }

    esp_err_t err = esp_http_client_perform(client);
    resp->status_code = esp_http_client_get_status_code(client);
    resp->content_length = esp_http_client_get_content_length(client);

    esp_http_client_cleanup(client);

    if (resp->truncated) {
        ESP_LOGW(TAG, "POST %s: body exceeded %u bytes and was truncated",
                 url, (unsigned)sizeof(resp->data));
    }
    ESP_LOGI(TAG, "HTTP POST request to %s completed with status %d", url, resp->status_code);
    return err;
}

esp_err_t http_get_stream(const char *url, const http_header_t *headers, size_t headers_count,
                          int timeout_ms, http_chunk_cb_t on_chunk, void *ctx,
                          http_stream_meta_t *out_meta) {
    if (!url || !on_chunk) {
        return ESP_ERR_INVALID_ARG;
    }

    if (out_meta) {
        memset(out_meta, 0, sizeof(*out_meta));
        out_meta->content_length = -1;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = HTTP_STREAM_BUF_SIZE,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    for (size_t i = 0; i < headers_count; i++) {
        esp_http_client_set_header(client, headers[i].key, headers[i].value);
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stream GET %s: open failed: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    bool chunked = esp_http_client_is_chunked_response(client);

    if (out_meta) {
        out_meta->status_code = status_code;
        out_meta->content_length = (int)content_length;
        out_meta->chunked = chunked;
    }

    ESP_LOGI(TAG, "stream GET %s: status %d, length %lld%s", url, status_code,
             (long long)content_length, chunked ? " (chunked)" : "");

    char *buf = malloc(HTTP_STREAM_BUF_SIZE);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    while (err == ESP_OK) {
        int read_len = esp_http_client_read(client, buf, HTTP_STREAM_BUF_SIZE);
        if (read_len < 0) {
            ESP_LOGE(TAG, "stream GET %s: read failed", url);
            err = ESP_FAIL;
            break;
        }
        if (read_len == 0) {
            break; // EOF
        }
        if (!on_chunk(ctx, buf, (size_t)read_len)) {
            ESP_LOGW(TAG, "stream GET %s: aborted by consumer", url);
            break;
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}
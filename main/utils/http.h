#pragma once

#include <stdbool.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

#define MAX_HEADERS_SIZE 512
#define MAX_BODY_SIZE 2048

typedef struct {
    int status_code;
    int content_length;
    char headers[MAX_HEADERS_SIZE];
    char data[MAX_BODY_SIZE];
    size_t data_len;
} http_response_t;

// Some utils for http GET and POST requests to external servers
esp_err_t http_get(const char *url, http_response_t *resp);
esp_err_t http_post(const char *url, const char *post_data, const char *content_type, http_response_t *resp);
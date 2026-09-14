#pragma once

#include <stdbool.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

#define MAX_HEADERS_SIZE 512
#define MAX_BODY_SIZE 8192

typedef struct {
    int status_code;
    int content_length;
    char headers[MAX_HEADERS_SIZE];
    char data[MAX_BODY_SIZE];
    size_t data_len;
    bool truncated; // body was larger than data[] and got cut off
} http_response_t;

typedef struct {
    const char *key;
    const char *value;
} http_header_t;

// Some utils for http GET and POST requests to external servers.
// `headers`/`headers_count` may be NULL/0 to send no extra request headers.
esp_err_t http_get(const char *url, const http_header_t *headers, size_t headers_count, http_response_t *resp);
esp_err_t http_post(const char *url, const char *post_data, const char *content_type,
                     const http_header_t *headers, size_t headers_count, http_response_t *resp);
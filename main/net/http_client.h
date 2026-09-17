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

// Streaming GET: reads the response body incrementally instead of buffering it
// whole, invoking on_chunk for every block of bytes as it arrives. Use for
// payloads too large for http_response_t (e.g. audio). Returning false from
// on_chunk aborts the transfer early.
typedef bool (*http_chunk_cb_t)(void *ctx, const char *data, size_t len);

typedef struct {
    int status_code;
    int content_length; // -1 when unknown (e.g. chunked transfer encoding)
    bool chunked;
} http_stream_meta_t;

// Some utils for http GET and POST requests to external servers.
// `headers`/`headers_count` may be NULL/0 to send no extra request headers.
// The default functions use a 5 s timeout; the _with_timeout variants let
// callers pick one (use e.g. 120000 ms for long downloads/imports).
esp_err_t http_get(const char *url, const http_header_t *headers, size_t headers_count, http_response_t *resp);
esp_err_t http_post(const char *url, const char *post_data, const char *content_type,
                     const http_header_t *headers, size_t headers_count, http_response_t *resp);
esp_err_t http_get_with_timeout(const char *url, const http_header_t *headers, size_t headers_count,
                                 int timeout_ms, http_response_t *resp);
esp_err_t http_post_with_timeout(const char *url, const char *post_data, const char *content_type,
                                  const http_header_t *headers, size_t headers_count,
                                  int timeout_ms, http_response_t *resp);

// GET `url` and stream the body through on_chunk without buffering it.
// out_meta is filled in once the headers are parsed (may be NULL).
esp_err_t http_get_stream(const char *url, const http_header_t *headers, size_t headers_count,
                          int timeout_ms, http_chunk_cb_t on_chunk, void *ctx,
                          http_stream_meta_t *out_meta);
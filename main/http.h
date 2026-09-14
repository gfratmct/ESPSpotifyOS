#pragma once

#include <stdbool.h>
#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_http_client.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

#define MAX_HEADERS_SIZE 512
#define MAX_BODY_SIZE 2048

typedef struct {
    httpd_handle_t server;
    bool is_running;
    int port;
    char root_path[128];
} webserver_t;

typedef struct {
    int status_code;
    int content_length;
    char headers[MAX_HEADERS_SIZE];
    char data[MAX_BODY_SIZE];
    size_t data_len;
} http_response_t;

// Lifecycle functions
webserver_t *webserver_create(int port);
esp_err_t webserver_start(webserver_t *webserver);
void webserver_stop(webserver_t *webserver);
void webserver_destroy(webserver_t *webserver);

// Global instance access
webserver_t *webserver_get_instance(void);

// Route registration
esp_err_t route_get_path(webserver_t *webserver, const char *path, esp_err_t (*handler)(httpd_req_t *r));
esp_err_t route_post_path(webserver_t *webserver, const char *path, esp_err_t (*handler)(httpd_req_t *r));
esp_err_t route_put_path(webserver_t *webserver, const char *path, esp_err_t (*handler)(httpd_req_t *r));
esp_err_t route_delete_path(webserver_t *webserver, const char *path, esp_err_t (*handler)(httpd_req_t *r));

// Some utils for http GET and POST requests to external servers
esp_err_t http_get(const char *url, http_response_t *resp);
esp_err_t http_post(const char *url, const char *post_data, const char *content_type, http_response_t *resp);
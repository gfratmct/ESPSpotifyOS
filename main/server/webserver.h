#pragma once

#include <stdbool.h>

#include <esp_err.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    httpd_handle_t server;
    bool is_running;
    int port;
} webserver_t;

// Lifecycle
webserver_t *webserver_create(int port);
esp_err_t webserver_start(webserver_t *webserver);
void webserver_stop(webserver_t *webserver);
void webserver_destroy(webserver_t *webserver);

// Registers an HTTP handler for `path`/`method`. The webserver must be started.
esp_err_t webserver_register_route(webserver_t *webserver, httpd_method_t method,
                                   const char *path, esp_err_t (*handler)(httpd_req_t *r));

#ifdef __cplusplus
}
#endif

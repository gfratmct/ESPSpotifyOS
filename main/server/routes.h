#pragma once

#include "server/webserver.h"

#ifdef __cplusplus
extern "C" {
#endif

// Registers the application's HTTP endpoints on a running webserver.
esp_err_t routes_register(webserver_t *webserver);

#ifdef __cplusplus
}
#endif

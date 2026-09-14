#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_TOKEN_MAX_LEN 512

typedef struct {
    uint32_t volume;
    uint32_t mode;
} settings_t;

esp_err_t settings_load(settings_t *out);
esp_err_t settings_save_volume(uint32_t volume);
esp_err_t settings_save_mode(uint32_t mode);

esp_err_t settings_spotify_get_refresh_token(char *buf, size_t len);
esp_err_t settings_spotify_set_refresh_token(const char *token);
esp_err_t settings_spotify_get_access_token(char *buf, size_t len);
esp_err_t settings_spotify_set_access_token(const char *token);
esp_err_t settings_spotify_get_token_expiry(uint64_t *out);
esp_err_t settings_spotify_set_token_expiry(uint64_t epoch_secs);

#ifdef __cplusplus
}
#endif
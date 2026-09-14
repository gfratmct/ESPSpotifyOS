#include "settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG "settings"
#define SETTINGS_NAMESPACE "settings"

#define KEY_VOLUME "volume"
#define KEY_MODE "mode"
#define KEY_SP_REFRESH "sp_refresh"
#define KEY_SP_ACCESS "sp_access"
#define KEY_SP_EXPIRY "sp_expiry"

/* Tokens are obfuscated with a fixed XOR key before being stored in NVS so the
 * raw value is not trivially readable from a flash dump. This is obfuscation,
 * not encryption: a determined attacker with the binary can recover the key. */
static const uint8_t s_obfuscation_key[] = {0xA7, 0x3C, 0x5E, 0x91, 0x4B, 0xD6, 0x2F, 0x18};

static void obfuscate(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        buf[i] ^= s_obfuscation_key[i % sizeof(s_obfuscation_key)];
    }
}

static esp_err_t read_u32(const char *key, uint32_t *val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_u32(h, key, val);
    nvs_close(h);
    return err;
}

static esp_err_t write_u32(const char *key, uint32_t val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32(h, key, val);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t settings_load(settings_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    out->volume = 60;
    out->mode = 0;
    uint32_t v = 0;
    if (read_u32(KEY_VOLUME, &v) == ESP_OK) {
        out->volume = v > 100 ? 100 : v;
    }
    if (read_u32(KEY_MODE, &v) == ESP_OK) {
        out->mode = v;
    }
    return ESP_OK;
}

esp_err_t settings_save_volume(uint32_t volume)
{
    return write_u32(KEY_VOLUME, volume > 100 ? 100 : volume);
}

esp_err_t settings_save_mode(uint32_t mode)
{
    return write_u32(KEY_MODE, mode);
}

static esp_err_t get_blob(const char *key, char *buf, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t stored = len;
    err = nvs_get_blob(h, key, buf, &stored);
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }
    obfuscate((uint8_t *)buf, stored);
    if (stored < len) {
        buf[stored] = '\0';
    }
    return ESP_OK;
}

static esp_err_t set_blob(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = strlen(value);
    uint8_t *tmp = (uint8_t *)malloc(len ? len : 1);
    if (tmp == NULL) {
        nvs_close(h);
        return ESP_ERR_NO_MEM;
    }
    memcpy(tmp, value, len);
    obfuscate(tmp, len);
    err = nvs_set_blob(h, key, tmp, len);
    free(tmp);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t settings_spotify_get_refresh_token(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = get_blob(KEY_SP_REFRESH, buf, len);
    return err;
}

esp_err_t settings_spotify_set_refresh_token(const char *token)
{
    if (token == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return set_blob(KEY_SP_REFRESH, token);
}

esp_err_t settings_spotify_get_access_token(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return get_blob(KEY_SP_ACCESS, buf, len);
}

esp_err_t settings_spotify_set_access_token(const char *token)
{
    if (token == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return set_blob(KEY_SP_ACCESS, token);
}

esp_err_t settings_spotify_get_token_expiry(uint64_t *out)
{
    uint32_t v = 0;
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = read_u32(KEY_SP_EXPIRY, &v);
    if (err == ESP_OK) {
        *out = (uint64_t)v;
    }
    return err;
}

esp_err_t settings_spotify_set_token_expiry(uint64_t epoch_secs)
{
    return write_u32(KEY_SP_EXPIRY, (uint32_t)epoch_secs);
}
#include "storage/sd_storage.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#define TAG "sd_storage"

#define SD_SPI_HOST SPI2_HOST
#define SD_MAX_FILES 5

static sdmmc_card_t *s_card;
static bool s_mounted;
static bool s_mount_attempted;
static bool s_bus_initialized;

static esp_err_t spi_bus_init_once(void)
{
    if (s_bus_initialized) {
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_PLAYER_SD_MOSI_GPIO,
        .miso_io_num = CONFIG_PLAYER_SD_MISO_GPIO,
        .sclk_io_num = CONFIG_PLAYER_SD_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_bus_initialized = true;
    return ESP_OK;
}

esp_err_t sd_storage_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    if (s_mount_attempted) {
        // A previous attempt failed (usually no card inserted). Don't retry:
        // the half-initialized sdspi device would report GPIO conflicts.
        return ESP_ERR_INVALID_STATE;
    }
    s_mount_attempted = true;

    ESP_LOGI(TAG, "mounting SD: MOSI=%d MISO=%d SCK=%d CS=%d CD=%d",
             CONFIG_PLAYER_SD_MOSI_GPIO, CONFIG_PLAYER_SD_MISO_GPIO,
             CONFIG_PLAYER_SD_SCK_GPIO, CONFIG_PLAYER_SD_CS_GPIO, CONFIG_PLAYER_SD_CD_GPIO);

#if CONFIG_PLAYER_SD_CD_GPIO >= 0
    // CD polarity varies by socket, so it only gets logged, never used to skip the mount.
    gpio_config_t cd_io = {
        .pin_bit_mask = 1ULL << CONFIG_PLAYER_SD_CD_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    if (gpio_config(&cd_io) == ESP_OK) {
        ESP_LOGI(TAG, "card-detect GPIO%d level=%d (usually 0 = card inserted)",
                 CONFIG_PLAYER_SD_CD_GPIO, gpio_get_level(CONFIG_PLAYER_SD_CD_GPIO));
    }
#endif

    // SD cards in SPI mode need pull-ups on MISO and CS; the PCB may not have them.
    gpio_pullup_en(CONFIG_PLAYER_SD_MISO_GPIO);
    gpio_pullup_en(CONFIG_PLAYER_SD_CS_GPIO);

    esp_err_t err = spi_bus_init_once();
    if (err != ESP_OK) {
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SD_SPI_HOST;
    slot.gpio_cs = CONFIG_PLAYER_SD_CS_GPIO;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(CONFIG_PLAYER_SD_MOUNT_POINT, &host, &slot,
                                  &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "timeout = card never answered: no card, card not FAT32, "
                          "or MOSI/MISO/SCK/CS wiring. SD disabled until reboot.");
        }
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", CONFIG_PLAYER_SD_MOUNT_POINT);
    return ESP_OK;
}

esp_err_t sd_storage_ensure_mounted(void)
{
    return sd_storage_mount();
}

bool sd_storage_ready(void)
{
    return s_mounted;
}

const char *sd_storage_mount_point(void)
{
    return CONFIG_PLAYER_SD_MOUNT_POINT;
}

const char *sd_storage_music_dir(void)
{
    return CONFIG_PLAYER_MUSIC_DIR;
}

esp_err_t sd_storage_ensure_music_dir(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[288];
    snprintf(path, sizeof(path), "%s%s", CONFIG_PLAYER_SD_MOUNT_POINT, CONFIG_PLAYER_MUSIC_DIR);

    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (mkdir(path, 0775) != 0) {
        ESP_LOGE(TAG, "mkdir %s failed", path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool has_audio_extension(const char *name)
{
    static const char *exts[] = { "mp3", "wav", "aac", "flac", "ogg", "opus" };
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1]) {
        return false;
    }

    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (strcasecmp(dot + 1, exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

esp_err_t sd_storage_list_music(int offset, int limit, sd_file_t *out, size_t max,
                                size_t *out_count, int *out_total)
{
    if (out_count) {
        *out_count = 0;
    }
    if (out_total) {
        *out_total = 0;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char dir[288];
    snprintf(dir, sizeof(dir), "%s%s", CONFIG_PLAYER_SD_MOUNT_POINT, CONFIG_PLAYER_MUSIC_DIR);

    DIR *d = opendir(dir);
    if (!d) {
        ESP_LOGW(TAG, "cannot open %s", dir);
        return ESP_ERR_NOT_FOUND;
    }

    int seen = 0;
    size_t taken = 0;
    int total = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' || !has_audio_extension(ent->d_name)) {
            continue;
        }

        total++;
        if (seen++ < offset) {
            continue;
        }
        if (taken >= max || (limit > 0 && taken >= (size_t)limit)) {
            continue;
        }

        sd_file_t *f = &out[taken++];
        memset(f, 0, sizeof(*f));
        strncpy(f->name, ent->d_name, sizeof(f->name) - 1);
        snprintf(f->path, sizeof(f->path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(f->path, &st) == 0) {
            f->size_bytes = (int)st.st_size;
        }
    }
    closedir(d);

    if (out_count) {
        *out_count = taken;
    }
    if (out_total) {
        *out_total = total;
    }
    return ESP_OK;
}

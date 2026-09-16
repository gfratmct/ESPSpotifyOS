#include "utils/blob_store.h"

#include <stdbool.h>
#include <esp_log.h>

#define TAG "blob_store"

static bool s_open;
static int s_track_id;
static size_t s_bytes_written;

esp_err_t blob_store_open(int track_id, const char *encoding)
{
    s_open = true;
    s_track_id = track_id;
    s_bytes_written = 0;

    const char *ext = (encoding && encoding[0]) ? encoding : "bin";
    ESP_LOGI(TAG, "TODO: persist stream to %s%s/%d.%s (SD FatFS not mounted yet)",
             CONFIG_PLAYER_SD_MOUNT_POINT, CONFIG_PLAYER_MUSIC_DIR, track_id, ext);
    return ESP_OK;
}

esp_err_t blob_store_write(const void *data, size_t len)
{
    if (!s_open) return ESP_ERR_INVALID_STATE;
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    // TODO: fwrite(data, 1, len, s_file);
    s_bytes_written += len;
    return ESP_OK;
}

esp_err_t blob_store_close(void)
{
    if (!s_open) return ESP_ERR_INVALID_STATE;

    // TODO: fclose(s_file);
    ESP_LOGI(TAG, "TODO: close blob for track %d (%u bytes)",
             s_track_id, (unsigned)s_bytes_written);
    s_open = false;
    return ESP_OK;
}

size_t blob_store_bytes_written(void)
{
    return s_bytes_written;
}

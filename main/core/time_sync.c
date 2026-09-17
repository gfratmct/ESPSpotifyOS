#include "core/time_sync.h"

#include <esp_log.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "time_sync"

void time_sync_start(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP time sync started");
}

bool time_sync_wait(uint32_t timeout_ms)
{
    TickType_t started_at = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        if (xTaskGetTickCount() - started_at >= timeout_ticks) {
            ESP_LOGW(TAG, "SNTP time sync timed out");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "SNTP time synchronized");
    return true;
}

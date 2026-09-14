#include "app_actions.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "http.h"

#define TAG "app_actions"

void act_spotify_connect(void)
{
    ESP_LOGI(TAG, "connect_to_spotify pressed — starting OAuth flow");

    http_response_t resp;
    esp_err_t err = http_get("http://127.0.0.1:8080/submit", &resp);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET successful: %s", resp.data);
        ESP_LOGI(TAG, "   - GET headers: %s", resp.headers);
        ESP_LOGI(TAG, "   - GET content length: %d", resp.content_length);
        ESP_LOGI(TAG, "   - GET data length: %d", resp.data_len);
        ESP_LOGI(TAG, "   - GET status code: %d", resp.status_code);
    } else {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
    }
}
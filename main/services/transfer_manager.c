#include "services/transfer_manager.h"

#include <stdlib.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "net/media_client.h"
#include "services/track_cache.h"

#define TAG "transfer_manager"

#define TRANSFER_QUEUE_LEN  1
#define TRANSFER_TASK_STACK 8192
#define TRANSFER_TASK_PRIO  4

typedef enum {
    TRANSFER_KIND_IMPORT,   // search + import into the media server, then cache
    TRANSFER_KIND_DOWNLOAD, // download a known media-server track into the cache
} transfer_kind_t;

typedef struct {
    transfer_kind_t kind;
    char query[320];        // IMPORT: "artist - title"
    int track_id;           // DOWNLOAD
    char encoding[16];      // DOWNLOAD
    char display_name[192]; // file name for the cache
} transfer_request_t;

// Written by the worker task, consumed (after the state flag) by the LVGL task.
static volatile transfer_state_t s_state = TRANSFER_IDLE;
static char s_error[96];
static QueueHandle_t s_queue;
static TaskHandle_t s_task;

static void run_import(const transfer_request_t *req)
{
    bool created = false;
    media_track_t result;
    esp_err_t err = media_import_query(req->query, &result, &created);
    if (err != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "%s", esp_err_to_name(err));
        ESP_LOGE(TAG, "import failed: %s", s_error);
        s_state = TRANSFER_DONE_FAILED;
        return;
    }

    ESP_LOGI(TAG, "import ok: '%s' (id %d, %s)",
             result.title, result.id, created ? "new" : "already present");
    s_state = created ? TRANSFER_DONE_NEW : TRANSFER_DONE_DUPLICATE;

    // best-effort: keep a local copy so the track is available offline
    bool cached = false;
    if (track_cache_fetch(result.id, result.encoding, req->query, &cached) == ESP_OK) {
        ESP_LOGI(TAG, "cache: %s", cached ? "stored" : "already present");
    }
}

static void run_download(const transfer_request_t *req)
{
    bool created = false;
    esp_err_t err = track_cache_fetch(req->track_id, req->encoding, req->display_name, &created);
    if (err != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "%s", esp_err_to_name(err));
        ESP_LOGE(TAG, "download failed: %s", s_error);
        s_state = TRANSFER_DONE_FAILED;
        return;
    }

    ESP_LOGI(TAG, "download ok: '%s' (%s)", req->display_name, created ? "new" : "already cached");
    s_state = created ? TRANSFER_DONE_NEW : TRANSFER_DONE_DUPLICATE;
}

static void transfer_task(void *arg)
{
    (void)arg;
    for (;;) {
        transfer_request_t *req = NULL;
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE || !req) {
            continue;
        }

        if (req->kind == TRANSFER_KIND_IMPORT) {
            run_import(req);
        } else {
            run_download(req);
        }
        free(req);
    }
}

// Lazily creates the worker task + queue. Must not be called concurrently.
static bool ensure_worker(void)
{
    if (!s_queue) {
        s_queue = xQueueCreate(TRANSFER_QUEUE_LEN, sizeof(transfer_request_t *));
        if (!s_queue) return false;
    }
    if (!s_task) {
        if (xTaskCreate(transfer_task, "transfer", TRANSFER_TASK_STACK, NULL,
                        TRANSFER_TASK_PRIO, &s_task) != pdPASS) {
            ESP_LOGE(TAG, "failed to create transfer task");
            return false;
        }
    }
    return true;
}

static bool enqueue(transfer_request_t *req)
{
    if (!req) return false;
    if (s_state == TRANSFER_RUNNING || !ensure_worker()) {
        free(req);
        return false;
    }
    if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "transfer queue full, dropping request");
        free(req);
        return false;
    }
    s_state = TRANSFER_RUNNING;
    return true;
}

bool transfer_manager_import(const char *query)
{
    if (!query || !*query) return false;

    transfer_request_t *req = calloc(1, sizeof(*req));
    if (!req) return false;
    req->kind = TRANSFER_KIND_IMPORT;
    snprintf(req->query, sizeof(req->query), "%s", query);
    return enqueue(req);
}

bool transfer_manager_download(int track_id, const char *encoding, const char *display_name)
{
    transfer_request_t *req = calloc(1, sizeof(*req));
    if (!req) return false;
    req->kind = TRANSFER_KIND_DOWNLOAD;
    req->track_id = track_id;
    snprintf(req->encoding, sizeof(req->encoding), "%s", encoding ? encoding : "");
    snprintf(req->display_name, sizeof(req->display_name), "%s", display_name ? display_name : "track");
    return enqueue(req);
}

transfer_state_t transfer_manager_state(void)
{
    return s_state;
}

const char *transfer_manager_error(void)
{
    return s_error;
}

void transfer_manager_clear(void)
{
    s_error[0] = '\0';
    s_state = TRANSFER_IDLE;
}

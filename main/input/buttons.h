#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_UP = 0,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_CENTER,
    BTN_CENTER_LONG,
    BTN_UP_LONG, // Top held for 5 s: AUX test melody
} button_id_t;

typedef struct {
    button_id_t id;
} button_event_t;

/**
 * @brief Configure the five switches and start the debounce task.
 *        Events are pushed to an internal FreeRTOS queue.
 */
esp_err_t buttons_init(void);

/**
 * @brief Pop the next button event. Returns false if the queue is empty.
 */
bool buttons_pop_event(button_event_t *out, TickType_t timeout);

/**
 * @brief Access the event queue (for advanced waiters). Do not delete it.
 */
QueueHandle_t buttons_queue(void);

// Human-readable name for logs ("UP", "CENTER_LONG", ...).
const char *button_name(button_id_t id);

#ifdef __cplusplus
}
#endif

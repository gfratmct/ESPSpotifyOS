#include "input/buttons.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define TAG "buttons"

#define BTN_DEBOUNCE_MS   40
#define BTN_LONG_PRESS_MS 700
#define BTN_TOP_HOLD_MS   5000
#define BTN_POLL_MS       20
#define BTN_QUEUE_LEN     16
#define BTN_TASK_STACK    3072
#define BTN_TASK_PRIO     4

typedef struct {
    gpio_num_t gpio;
    button_id_t short_id;
    button_id_t long_id;
    int long_press_ms; // 0 = release always sends the short event
    bool stable_pressed;
    bool long_fired;
    int raw_pressed;
    int debounce_ms;
    int held_ms;
} btn_state_t;

static QueueHandle_t s_queue;
static btn_state_t s_btns[] = {
    { CONFIG_PLAYER_BTN_TOP_GPIO,    BTN_UP,     BTN_UP_LONG,     BTN_TOP_HOLD_MS,   false, false, 0, 0, 0 },
    { CONFIG_PLAYER_BTN_BOTTOM_GPIO, BTN_DOWN,   BTN_DOWN,        0,                 false, false, 0, 0, 0 },
    { CONFIG_PLAYER_BTN_LEFT_GPIO,   BTN_LEFT,   BTN_LEFT,        0,                 false, false, 0, 0, 0 },
    { CONFIG_PLAYER_BTN_RIGHT_GPIO,  BTN_RIGHT,  BTN_RIGHT,       0,                 false, false, 0, 0, 0 },
    { CONFIG_PLAYER_BTN_CENTER_GPIO, BTN_CENTER, BTN_CENTER_LONG, BTN_LONG_PRESS_MS, false, false, 0, 0, 0 },
};

const char *button_name(button_id_t id)
{
    switch (id) {
    case BTN_UP: return "UP";
    case BTN_DOWN: return "DOWN";
    case BTN_LEFT: return "LEFT";
    case BTN_RIGHT: return "RIGHT";
    case BTN_CENTER: return "CENTER";
    case BTN_CENTER_LONG: return "CENTER_LONG";
    case BTN_UP_LONG: return "UP_LONG";
    default: return "?";
    }
}

static void push_event(button_id_t id)
{
    button_event_t ev = { .id = id };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropped %s", button_name(id));
        return;
    }
    ESP_LOGI(TAG, "event %s", button_name(id));
}

static void buttons_task(void *arg)
{
    (void)arg;
    while (1) {
        for (size_t i = 0; i < sizeof(s_btns) / sizeof(s_btns[0]); i++) {
            btn_state_t *b = &s_btns[i];
            // Active-low with internal pull-up.
            const bool raw = gpio_get_level(b->gpio) == 0;

            if (raw == b->raw_pressed) {
                b->debounce_ms += BTN_POLL_MS;
            } else {
                b->raw_pressed = raw;
                b->debounce_ms = 0;
            }

            if (b->debounce_ms < BTN_DEBOUNCE_MS) {
                continue;
            }

            if (raw && !b->stable_pressed) {
                b->stable_pressed = true;
                b->long_fired = false;
                b->held_ms = 0;
                ESP_LOGD(TAG, "%s pressed (GPIO%d)", button_name(b->short_id), b->gpio);
            } else if (raw && b->stable_pressed) {
                b->held_ms += BTN_POLL_MS;
                // Fire once, while still held, so a 5 s Top press is not also a short Up.
                if (b->long_press_ms > 0 && !b->long_fired && b->held_ms >= b->long_press_ms) {
                    b->long_fired = true;
                    push_event(b->long_id);
                }
            } else if (!raw && b->stable_pressed) {
                b->stable_pressed = false;
                ESP_LOGD(TAG, "%s released after %d ms", button_name(b->short_id), b->held_ms);
                if (!b->long_fired) {
                    push_event(b->short_id);
                }
                b->held_ms = 0;
                b->long_fired = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

esp_err_t buttons_init(void)
{
    if (s_queue) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(BTN_QUEUE_LEN, sizeof(button_event_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }

    uint64_t mask = 0;
    for (size_t i = 0; i < sizeof(s_btns) / sizeof(s_btns[0]); i++) {
        mask |= 1ULL << s_btns[i].gpio;
    }

    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    // Idle level must read 1 (pull-up, switch open). A 0 here means the switch
    // is stuck, wired active-high, or the GPIO is shorted.
    vTaskDelay(pdMS_TO_TICKS(5));
    for (size_t i = 0; i < sizeof(s_btns) / sizeof(s_btns[0]); i++) {
        const int level = gpio_get_level(s_btns[i].gpio);
        if (level == 0) {
            ESP_LOGW(TAG, "%-6s GPIO%-2d idle level=0 (stuck or wired active-high?)",
                     button_name(s_btns[i].short_id), s_btns[i].gpio);
        } else {
            ESP_LOGI(TAG, "%-6s GPIO%-2d idle level=1 OK",
                     button_name(s_btns[i].short_id), s_btns[i].gpio);
        }
    }

    BaseType_t ok = xTaskCreate(buttons_task, "buttons", BTN_TASK_STACK, NULL,
                                BTN_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "switches ready (center long %d ms, top hold %d ms)",
             BTN_LONG_PRESS_MS, BTN_TOP_HOLD_MS);
    return ESP_OK;
}

bool buttons_pop_event(button_event_t *out, TickType_t timeout)
{
    if (!s_queue || !out) {
        return false;
    }
    return xQueueReceive(s_queue, out, timeout) == pdTRUE;
}

QueueHandle_t buttons_queue(void)
{
    return s_queue;
}

#include "led_status.h"

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "LED_STATUS";
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static led_status_state_t s_state = LED_STATUS_BOOT;

static void apply_pattern_levels(const led_pattern_t *pattern, bool active)
{
    const board_config_t *board = board_config_get();

    gpio_set_level(board->led_pin_1, active && pattern->led1_on ? 1 : 0);
    gpio_set_level(board->led_pin_2, active && pattern->led2_on ? 1 : 0);
}

esp_err_t led_status_get_pattern(led_status_state_t state, led_pattern_t *pattern)
{
    if (pattern == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (state) {
    case LED_STATUS_BOOT:
        *pattern = (led_pattern_t){ .on_ms = 120, .off_ms = 120, .repeat = true, .led1_on = true, .led2_on = true };
        break;
    case LED_STATUS_PROVISIONING:
        *pattern = (led_pattern_t){ .on_ms = 200, .off_ms = 200, .repeat = true, .led1_on = true, .led2_on = false };
        break;
    case LED_STATUS_WIFI_CONNECTING:
        *pattern = (led_pattern_t){ .on_ms = 120, .off_ms = 680, .repeat = true, .led1_on = false, .led2_on = true };
        break;
    case LED_STATUS_PENDING_APPROVAL:
        *pattern = (led_pattern_t){ .on_ms = 90, .off_ms = 160, .repeat = true, .led1_on = true, .led2_on = true };
        break;
    case LED_STATUS_STREAMING:
        *pattern = (led_pattern_t){ .on_ms = 0, .off_ms = 0, .repeat = false, .led1_on = true, .led2_on = false };
        break;
    case LED_STATUS_FAULT:
        *pattern = (led_pattern_t){ .on_ms = 0, .off_ms = 0, .repeat = false, .led1_on = false, .led2_on = true };
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static void led_status_task(void *arg)
{
    (void)arg;

    while (true) {
        led_pattern_t pattern = {0};
        led_status_state_t state;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        state = s_state;
        xSemaphoreGive(s_mutex);

        if (led_status_get_pattern(state, &pattern) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!pattern.repeat) {
            apply_pattern_levels(&pattern, true);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        apply_pattern_levels(&pattern, true);
        vTaskDelay(pdMS_TO_TICKS(pattern.on_ms));
        apply_pattern_levels(&pattern, false);
        vTaskDelay(pdMS_TO_TICKS(pattern.off_ms));
    }
}

esp_err_t led_status_init(void)
{
    const board_config_t *board = board_config_get();
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << board->led_pin_1) | (1ULL << board->led_pin_2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret;

    ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_task == NULL) {
        BaseType_t created = xTaskCreate(led_status_task, "led_status", 3072, NULL, 2, &s_task);
        if (created != pdPASS) {
            s_task = NULL;
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "led status ready on pins %d and %d", (int)board->led_pin_1, (int)board->led_pin_2);
    return led_status_set(LED_STATUS_BOOT);
}

esp_err_t led_status_set(led_status_state_t state)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = state;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

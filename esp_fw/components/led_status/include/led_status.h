#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATUS_BOOT = 0,
    LED_STATUS_PROVISIONING,
    LED_STATUS_WIFI_CONNECTING,
    LED_STATUS_PENDING_APPROVAL,
    LED_STATUS_STREAMING,
    LED_STATUS_FAULT,
} led_status_state_t;

typedef struct {
    uint32_t on_ms;
    uint32_t off_ms;
    bool repeat;
    bool led1_on;
    bool led2_on;
} led_pattern_t;

esp_err_t led_status_init(void);
esp_err_t led_status_set(led_status_state_t state);
esp_err_t led_status_get_pattern(led_status_state_t state, led_pattern_t *pattern);

#ifdef __cplusplus
}
#endif

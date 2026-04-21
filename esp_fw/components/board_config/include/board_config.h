#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_port_num_t i2c_port;
    gpio_num_t i2c_sda_pin;
    gpio_num_t i2c_scl_pin;
    gpio_num_t led_pin_1;
    gpio_num_t led_pin_2;
    uint32_t max30102_period_ms;
    uint32_t mpu6050_period_ms;
    uint32_t telemetry_period_ms;
} board_config_t;

const board_config_t *board_config_get(void);
esp_err_t board_config_init(void);

#ifdef __cplusplus
}
#endif

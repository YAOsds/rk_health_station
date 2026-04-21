#include "board_config.h"

#include "esp_log.h"

static const char *TAG = "BOARD_CFG";

static const board_config_t s_board_config = {
    .i2c_port = I2C_NUM_0,
    .i2c_sda_pin = GPIO_NUM_6,
    .i2c_scl_pin = GPIO_NUM_5,
    .led_pin_1 = GPIO_NUM_8,
    .led_pin_2 = GPIO_NUM_18,
    .max30102_period_ms = 2,
    .mpu6050_period_ms = 20,
    .telemetry_period_ms = 1000,
};

const board_config_t *board_config_get(void)
{
    return &s_board_config;
}

esp_err_t board_config_init(void)
{
    ESP_LOGI(TAG,
        "board config ready: i2c=%d sda=%d scl=%d led1=%d led2=%d",
        (int)s_board_config.i2c_port,
        (int)s_board_config.i2c_sda_pin,
        (int)s_board_config.i2c_scl_pin,
        (int)s_board_config.led_pin_1,
        (int)s_board_config.led_pin_2);
    return ESP_OK;
}

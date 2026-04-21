#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX30102_I2C_ADDRESS 0x57

#define MAX30102_REG_FIFO_WR_PTR 0x04
#define MAX30102_REG_FIFO_OVF_COUNTER 0x05
#define MAX30102_REG_FIFO_RD_PTR 0x06
#define MAX30102_REG_FIFO_DATA 0x07
#define MAX30102_REG_FIFO_CONFIG 0x08
#define MAX30102_REG_MODE_CONFIG 0x09
#define MAX30102_REG_SPO2_CONFIG 0x0A
#define MAX30102_REG_LED1_PULSE_AMP 0x0C
#define MAX30102_REG_LED2_PULSE_AMP 0x0D
#define MAX30102_REG_PART_ID 0xFF

#define MAX30102_MODE_SPO2 0x03

typedef enum {
    MAX30102_ADC_RANGE_2048 = 0x00,
    MAX30102_ADC_RANGE_4096 = 0x20,
    MAX30102_ADC_RANGE_8192 = 0x40,
    MAX30102_ADC_RANGE_16384 = 0x60,
} max30102_adc_range_t;

typedef enum {
    MAX30102_SAMPLE_RATE_50 = 0x00,
    MAX30102_SAMPLE_RATE_100 = 0x04,
    MAX30102_SAMPLE_RATE_200 = 0x08,
    MAX30102_SAMPLE_RATE_400 = 0x0C,
    MAX30102_SAMPLE_RATE_800 = 0x10,
    MAX30102_SAMPLE_RATE_1000 = 0x14,
    MAX30102_SAMPLE_RATE_1600 = 0x18,
    MAX30102_SAMPLE_RATE_3200 = 0x1C,
} max30102_sample_rate_t;

typedef enum {
    MAX30102_PULSE_WIDTH_69 = 0x00,
    MAX30102_PULSE_WIDTH_118 = 0x01,
    MAX30102_PULSE_WIDTH_215 = 0x02,
    MAX30102_PULSE_WIDTH_411 = 0x03,
} max30102_pulse_width_t;

typedef struct {
    i2c_port_num_t i2c_port;
    uint32_t bus_speed_hz;
    uint8_t red_led_amplitude;
    uint8_t ir_led_amplitude;
} max30102_config_t;

typedef struct {
    uint32_t red;
    uint32_t ir;
} max30102_sample_t;

typedef struct max30102_dev_s *max30102_handle_t;

uint8_t max30102_build_spo2_config(max30102_adc_range_t range,
    max30102_sample_rate_t sample_rate,
    max30102_pulse_width_t pulse_width);
esp_err_t max30102_init(const max30102_config_t *config, max30102_handle_t *handle);
esp_err_t max30102_configure(max30102_handle_t handle);
esp_err_t max30102_read_sample(max30102_handle_t handle, max30102_sample_t *sample);
esp_err_t max30102_clear_fifo(max30102_handle_t handle);
esp_err_t max30102_deinit(max30102_handle_t handle);

#ifdef __cplusplus
}
#endif

#include "max30102.h"

#include <stdlib.h>

#include "board_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX30102_XFER_TIMEOUT_MS 100

struct max30102_dev_s {
    i2c_port_num_t i2c_port;
    uint32_t bus_speed_hz;
    uint8_t red_led_amplitude;
    uint8_t ir_led_amplitude;
};

static const char *TAG = "MAX30102";

static esp_err_t write_register(max30102_handle_t handle, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_write_to_device(handle->i2c_port,
        MAX30102_I2C_ADDRESS,
        payload,
        sizeof(payload),
        pdMS_TO_TICKS(MAX30102_XFER_TIMEOUT_MS));
}

static esp_err_t read_register(max30102_handle_t handle, uint8_t reg, uint8_t *value)
{
    esp_err_t ret = i2c_master_write_to_device(handle->i2c_port,
        MAX30102_I2C_ADDRESS,
        &reg,
        1,
        pdMS_TO_TICKS(MAX30102_XFER_TIMEOUT_MS));
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_master_read_from_device(handle->i2c_port,
        MAX30102_I2C_ADDRESS,
        value,
        1,
        pdMS_TO_TICKS(MAX30102_XFER_TIMEOUT_MS));
}

uint8_t max30102_build_spo2_config(max30102_adc_range_t range,
    max30102_sample_rate_t sample_rate,
    max30102_pulse_width_t pulse_width)
{
    return (uint8_t)(range | sample_rate | pulse_width);
}

esp_err_t max30102_init(const max30102_config_t *config, max30102_handle_t *handle)
{
    const board_config_t *board = board_config_get();
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = board->i2c_sda_pin,
        .scl_io_num = board->i2c_scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
    };
    max30102_handle_t dev;
    uint8_t part_id = 0;
    esp_err_t ret;

    if (config == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    dev->i2c_port = config->i2c_port;
    dev->bus_speed_hz = config->bus_speed_hz != 0 ? config->bus_speed_hz : 400000;
    dev->red_led_amplitude = config->red_led_amplitude != 0 ? config->red_led_amplitude : 0x24;
    dev->ir_led_amplitude = config->ir_led_amplitude != 0 ? config->ir_led_amplitude : 0x24;
    i2c_conf.master.clk_speed = dev->bus_speed_hz;

    ret = i2c_param_config(dev->i2c_port, &i2c_conf);
    if (ret != ESP_OK) {
        free(dev);
        return ret;
    }

    ret = read_register(dev, MAX30102_REG_PART_ID, &part_id);
    if (ret != ESP_OK) {
        max30102_deinit(dev);
        return ret;
    }

    ESP_LOGI(TAG, "part id=0x%02x speed=%lu", part_id, (unsigned long)dev->bus_speed_hz);
    *handle = dev;
    return ESP_OK;
}

esp_err_t max30102_configure(max30102_handle_t handle)
{
    esp_err_t ret;

    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = write_register(handle, MAX30102_REG_MODE_CONFIG, 0x40);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ret = max30102_clear_fifo(handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = write_register(handle, MAX30102_REG_FIFO_CONFIG, 0x0F);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = write_register(handle, MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SPO2);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = write_register(handle, MAX30102_REG_SPO2_CONFIG,
        max30102_build_spo2_config(MAX30102_ADC_RANGE_16384,
            MAX30102_SAMPLE_RATE_100,
            MAX30102_PULSE_WIDTH_411));
    if (ret != ESP_OK) {
        return ret;
    }
    ret = write_register(handle, MAX30102_REG_LED1_PULSE_AMP, handle->red_led_amplitude);
    if (ret != ESP_OK) {
        return ret;
    }
    return write_register(handle, MAX30102_REG_LED2_PULSE_AMP, handle->ir_led_amplitude);
}

esp_err_t max30102_read_sample(max30102_handle_t handle, max30102_sample_t *sample)
{
    uint8_t raw[6] = {0};
    esp_err_t ret;

    if (handle == NULL || sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_master_write_to_device(handle->i2c_port,
        MAX30102_I2C_ADDRESS,
        (const uint8_t[]){MAX30102_REG_FIFO_DATA},
        1,
        pdMS_TO_TICKS(MAX30102_XFER_TIMEOUT_MS));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_master_read_from_device(handle->i2c_port,
        MAX30102_I2C_ADDRESS,
        raw,
        sizeof(raw),
        pdMS_TO_TICKS(MAX30102_XFER_TIMEOUT_MS));
    if (ret != ESP_OK) {
        return ret;
    }

    sample->red = ((((uint32_t)raw[0]) << 16) | (((uint32_t)raw[1]) << 8) | raw[2]) & 0x03FFFF;
    sample->ir = ((((uint32_t)raw[3]) << 16) | (((uint32_t)raw[4]) << 8) | raw[5]) & 0x03FFFF;
    return ESP_OK;
}

esp_err_t max30102_clear_fifo(max30102_handle_t handle)
{
    esp_err_t ret;

    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = write_register(handle, MAX30102_REG_FIFO_WR_PTR, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = write_register(handle, MAX30102_REG_FIFO_RD_PTR, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    return write_register(handle, MAX30102_REG_FIFO_OVF_COUNTER, 0);
}

esp_err_t max30102_deinit(max30102_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    free(handle);
    return ESP_OK;
}

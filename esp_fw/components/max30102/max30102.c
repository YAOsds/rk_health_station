#include "max30102.h"

#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX30102_XFER_TIMEOUT_MS 100

struct max30102_dev_s {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_port_num_t i2c_port;
    uint32_t bus_speed_hz;
    uint8_t red_led_amplitude;
    uint8_t ir_led_amplitude;
    bool owns_bus;
};

static const char *TAG = "MAX30102";

static esp_err_t ensure_i2c_bus(i2c_port_num_t port, i2c_master_bus_handle_t *bus_handle, bool *owns_bus)
{
    const board_config_t *board = board_config_get();
    i2c_master_bus_config_t bus_config = {
        .i2c_port = port,
        .sda_io_num = board->i2c_sda_pin,
        .scl_io_num = board->i2c_scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 4,
        .flags.enable_internal_pullup = 1,
    };
    esp_err_t ret;

    ret = i2c_master_get_bus_handle(port, bus_handle);
    if (ret == ESP_OK) {
        *owns_bus = false;
        return ESP_OK;
    }

    ret = i2c_new_master_bus(&bus_config, bus_handle);
    if (ret == ESP_OK) {
        *owns_bus = true;
    }
    return ret;
}

static esp_err_t write_register(max30102_handle_t handle, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(handle->dev_handle, payload, sizeof(payload), MAX30102_XFER_TIMEOUT_MS);
}

static esp_err_t read_register(max30102_handle_t handle, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(handle->dev_handle, &reg, 1, value, 1, MAX30102_XFER_TIMEOUT_MS);
}

uint8_t max30102_build_spo2_config(max30102_adc_range_t range,
    max30102_sample_rate_t sample_rate,
    max30102_pulse_width_t pulse_width)
{
    return (uint8_t)(range | sample_rate | pulse_width);
}

esp_err_t max30102_init(const max30102_config_t *config, max30102_handle_t *handle)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30102_I2C_ADDRESS,
        .scl_speed_hz = config != NULL && config->bus_speed_hz != 0 ? config->bus_speed_hz : 400000,
        .scl_wait_us = 0,
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
    dev->bus_speed_hz = dev_config.scl_speed_hz;
    dev->red_led_amplitude = config->red_led_amplitude != 0 ? config->red_led_amplitude : 0x24;
    dev->ir_led_amplitude = config->ir_led_amplitude != 0 ? config->ir_led_amplitude : 0x24;

    ret = ensure_i2c_bus(config->i2c_port, &dev->bus_handle, &dev->owns_bus);
    if (ret != ESP_OK) {
        free(dev);
        return ret;
    }

    ret = i2c_master_bus_add_device(dev->bus_handle, &dev_config, &dev->dev_handle);
    if (ret != ESP_OK) {
        if (dev->owns_bus) {
            i2c_del_master_bus(dev->bus_handle);
        }
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
    uint8_t reg = MAX30102_REG_FIFO_DATA;
    uint8_t raw[6] = {0};
    esp_err_t ret;

    if (handle == NULL || sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_master_transmit_receive(handle->dev_handle, &reg, 1, raw, sizeof(raw), MAX30102_XFER_TIMEOUT_MS);
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
    esp_err_t ret = ESP_OK;

    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->dev_handle != NULL) {
        ret = i2c_master_bus_rm_device(handle->dev_handle);
    }
    if (handle->owns_bus && handle->bus_handle != NULL) {
        i2c_del_master_bus(handle->bus_handle);
    }
    free(handle);
    return ret;
}

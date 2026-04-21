#include "mpu6050.h"

#include <math.h>
#include <stdlib.h>

#include "board_config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MPU6050_XFER_TIMEOUT_MS 100

struct mpu6050_dev_s {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_port_num_t i2c_port;
    uint32_t bus_speed_hz;
    mpu6050_accel_range_t accel_range;
    mpu6050_gyro_range_t gyro_range;
    bool owns_bus;
};

static const char *TAG = "MPU6050";

static float accel_lsb_per_g(mpu6050_accel_range_t range)
{
    switch (range) {
    case MPU6050_ACCEL_RANGE_4G:
        return 8192.0f;
    case MPU6050_ACCEL_RANGE_8G:
        return 4096.0f;
    case MPU6050_ACCEL_RANGE_16G:
        return 2048.0f;
    case MPU6050_ACCEL_RANGE_2G:
    default:
        return 16384.0f;
    }
}

static float gyro_lsb_per_dps(mpu6050_gyro_range_t range)
{
    switch (range) {
    case MPU6050_GYRO_RANGE_500:
        return 65.5f;
    case MPU6050_GYRO_RANGE_1000:
        return 32.8f;
    case MPU6050_GYRO_RANGE_2000:
        return 16.4f;
    case MPU6050_GYRO_RANGE_250:
    default:
        return 131.0f;
    }
}

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

static esp_err_t write_register(mpu6050_handle_t handle, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(handle->dev_handle, payload, sizeof(payload), MPU6050_XFER_TIMEOUT_MS);
}

static esp_err_t read_register(mpu6050_handle_t handle, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(handle->dev_handle, &reg, 1, value, 1, MPU6050_XFER_TIMEOUT_MS);
}

float mpu6050_convert_accel_raw(int16_t raw, mpu6050_accel_range_t range)
{
    return (float)raw / accel_lsb_per_g(range);
}

float mpu6050_convert_gyro_raw(int16_t raw, mpu6050_gyro_range_t range)
{
    return (float)raw / gyro_lsb_per_dps(range);
}

esp_err_t mpu6050_init(const mpu6050_config_t *config, mpu6050_handle_t *handle)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU6050_I2C_ADDRESS,
        .scl_speed_hz = config != NULL && config->bus_speed_hz != 0 ? config->bus_speed_hz : 400000,
        .scl_wait_us = 0,
    };
    mpu6050_handle_t dev;
    uint8_t who_am_i = 0;
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
    dev->accel_range = config->accel_range;
    dev->gyro_range = config->gyro_range;

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

    ret = read_register(dev, MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) {
        mpu6050_deinit(dev);
        return ret;
    }
    if (who_am_i != MPU6050_I2C_ADDRESS) {
        ESP_LOGW(TAG, "unexpected who_am_i=0x%02x", who_am_i);
    }

    ret = write_register(dev, MPU6050_REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) {
        mpu6050_deinit(dev);
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    ret = write_register(dev, MPU6050_REG_ACCEL_CONFIG, (uint8_t)(config->accel_range << 3));
    if (ret != ESP_OK) {
        mpu6050_deinit(dev);
        return ret;
    }
    ret = write_register(dev, MPU6050_REG_GYRO_CONFIG, (uint8_t)(config->gyro_range << 3));
    if (ret != ESP_OK) {
        mpu6050_deinit(dev);
        return ret;
    }
    ret = write_register(dev, MPU6050_REG_CONFIG, (uint8_t)config->bandwidth);
    if (ret != ESP_OK) {
        mpu6050_deinit(dev);
        return ret;
    }

    ESP_LOGI(TAG, "who_am_i=0x%02x speed=%lu", who_am_i, (unsigned long)dev->bus_speed_hz);
    *handle = dev;
    return ESP_OK;
}

esp_err_t mpu6050_read_sample(mpu6050_handle_t handle, mpu6050_sample_t *sample)
{
    uint8_t reg = MPU6050_REG_ACCEL_XOUT_H;
    uint8_t raw[14] = {0};
    int16_t raw_ax;
    int16_t raw_ay;
    int16_t raw_az;
    int16_t raw_temp;
    int16_t raw_gx;
    int16_t raw_gy;
    int16_t raw_gz;
    esp_err_t ret;

    if (handle == NULL || sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_master_transmit_receive(handle->dev_handle, &reg, 1, raw, sizeof(raw), MPU6050_XFER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    raw_ax = (int16_t)((raw[0] << 8) | raw[1]);
    raw_ay = (int16_t)((raw[2] << 8) | raw[3]);
    raw_az = (int16_t)((raw[4] << 8) | raw[5]);
    raw_temp = (int16_t)((raw[6] << 8) | raw[7]);
    raw_gx = (int16_t)((raw[8] << 8) | raw[9]);
    raw_gy = (int16_t)((raw[10] << 8) | raw[11]);
    raw_gz = (int16_t)((raw[12] << 8) | raw[13]);

    sample->accel_x_g = mpu6050_convert_accel_raw(raw_ax, handle->accel_range);
    sample->accel_y_g = mpu6050_convert_accel_raw(raw_ay, handle->accel_range);
    sample->accel_z_g = mpu6050_convert_accel_raw(raw_az, handle->accel_range);
    sample->accel_norm_g = sqrtf(sample->accel_x_g * sample->accel_x_g
        + sample->accel_y_g * sample->accel_y_g
        + sample->accel_z_g * sample->accel_z_g);
    sample->gyro_x_dps = mpu6050_convert_gyro_raw(raw_gx, handle->gyro_range);
    sample->gyro_y_dps = mpu6050_convert_gyro_raw(raw_gy, handle->gyro_range);
    sample->gyro_z_dps = mpu6050_convert_gyro_raw(raw_gz, handle->gyro_range);
    sample->temperature_c = ((float)raw_temp / 340.0f) + 36.53f;
    return ESP_OK;
}

esp_err_t mpu6050_deinit(mpu6050_handle_t handle)
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

#pragma once

#include <stdint.h>

#include "driver/i2c_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MPU6050_I2C_ADDRESS 0x68
#define MPU6050_REG_CONFIG 0x1A
#define MPU6050_REG_GYRO_CONFIG 0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_PWR_MGMT_1 0x6B
#define MPU6050_REG_WHO_AM_I 0x75

typedef enum {
    MPU6050_ACCEL_RANGE_2G = 0,
    MPU6050_ACCEL_RANGE_4G = 1,
    MPU6050_ACCEL_RANGE_8G = 2,
    MPU6050_ACCEL_RANGE_16G = 3,
} mpu6050_accel_range_t;

typedef enum {
    MPU6050_GYRO_RANGE_250 = 0,
    MPU6050_GYRO_RANGE_500 = 1,
    MPU6050_GYRO_RANGE_1000 = 2,
    MPU6050_GYRO_RANGE_2000 = 3,
} mpu6050_gyro_range_t;

typedef enum {
    MPU6050_BAND_260_HZ = 0,
    MPU6050_BAND_184_HZ = 1,
    MPU6050_BAND_94_HZ = 2,
    MPU6050_BAND_44_HZ = 3,
    MPU6050_BAND_21_HZ = 4,
    MPU6050_BAND_10_HZ = 5,
    MPU6050_BAND_5_HZ = 6,
} mpu6050_bandwidth_t;

typedef struct {
    i2c_port_num_t i2c_port;
    uint32_t bus_speed_hz;
    mpu6050_accel_range_t accel_range;
    mpu6050_gyro_range_t gyro_range;
    mpu6050_bandwidth_t bandwidth;
} mpu6050_config_t;

typedef struct {
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float accel_norm_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
    float temperature_c;
} mpu6050_sample_t;

typedef struct mpu6050_dev_s *mpu6050_handle_t;

float mpu6050_convert_accel_raw(int16_t raw, mpu6050_accel_range_t range);
float mpu6050_convert_gyro_raw(int16_t raw, mpu6050_gyro_range_t range);
esp_err_t mpu6050_init(const mpu6050_config_t *config, mpu6050_handle_t *handle);
esp_err_t mpu6050_read_sample(mpu6050_handle_t handle, mpu6050_sample_t *sample);
esp_err_t mpu6050_deinit(mpu6050_handle_t handle);

#ifdef __cplusplus
}
#endif

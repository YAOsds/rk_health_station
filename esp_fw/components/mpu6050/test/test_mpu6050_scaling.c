#include "mpu6050.h"
#include "unity.h"

void test_mpu6050_convert_accel_raw_scales_2g_range(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f,
        mpu6050_convert_accel_raw(16384, MPU6050_ACCEL_RANGE_2G));
}

#include "imu_window_buffer.h"
#include "unity.h"

void test_imu_window_buffer_reports_ready_after_full_window(void)
{
    imu_window_buffer_t buffer;
    imu_sample6_t sample = { .values = {1, 2, 3, 4, 5, 6} };

    imu_window_buffer_init(&buffer, 256, 32);
    for (int i = 0; i < 255; ++i) {
        TEST_ASSERT_FALSE(imu_window_buffer_push(&buffer, &sample));
    }
    TEST_ASSERT_TRUE(imu_window_buffer_push(&buffer, &sample));
}

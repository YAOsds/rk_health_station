#include "heart_rate_algo.h"
#include "unity.h"

void test_heart_rate_algo_rejects_low_quality_window(void)
{
    signal_quality_t quality = {
        .finger_detected = false,
        .confidence = 0.1f,
    };
    heart_rate_result_t result = {0};

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, heart_rate_algo_compute(&quality, &result));
}

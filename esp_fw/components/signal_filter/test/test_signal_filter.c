#include "signal_filter.h"
#include "unity.h"

void test_signal_filter_detects_finger_presence_from_ir_dc_level(void)
{
    signal_filter_window_t window = {
        .sample_count = 4,
        .ir_samples = {12000, 11800, 12100, 11950},
        .red_samples = {9800, 9700, 9900, 9850},
    };
    signal_quality_t quality = {0};

    TEST_ASSERT_EQUAL(ESP_OK, signal_filter_analyze_window(&window, &quality));
    TEST_ASSERT_TRUE(quality.finger_detected);
}

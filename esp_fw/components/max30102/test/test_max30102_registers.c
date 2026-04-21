#include "max30102.h"
#include "unity.h"

void test_max30102_spo2_config_builder_combines_range_rate_and_width(void)
{
    uint8_t value = max30102_build_spo2_config(
        MAX30102_ADC_RANGE_16384,
        MAX30102_SAMPLE_RATE_100,
        MAX30102_PULSE_WIDTH_411);

    TEST_ASSERT_EQUAL_HEX8(0x67, value);
}

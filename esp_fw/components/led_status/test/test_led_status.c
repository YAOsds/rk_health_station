#include "led_status.h"
#include "unity.h"

void test_led_status_maps_pending_auth_to_distinct_pattern(void)
{
    led_pattern_t pattern = {0};

    TEST_ASSERT_EQUAL(ESP_OK, led_status_get_pattern(LED_STATUS_PENDING_APPROVAL, &pattern));
    TEST_ASSERT_GREATER_THAN_UINT32(0, pattern.on_ms);
    TEST_ASSERT_GREATER_THAN_UINT32(0, pattern.off_ms);
    TEST_ASSERT_TRUE(pattern.repeat);
}

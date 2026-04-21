#include <string.h>

#include "unity.h"

#include "telemetry_uploader.h"

void test_telemetry_encoder_includes_required_fields(void)
{
    telemetry_vitals_t vitals = {
        .heart_rate = 72,
        .spo2 = 98.4f,
        .acceleration = 0.42f,
        .finger_detected = true,
    };
    char frame[512] = {0};

    TEST_ASSERT_EQUAL(ESP_OK,
        telemetry_uploader_build_frame("watch_001", "RK Watch 01", "1.0.0-rk", 12, 1713000010, &vitals,
            frame, sizeof(frame)));
    TEST_ASSERT_NOT_NULL(strstr(frame, "\"type\":\"telemetry_batch\""));
    TEST_ASSERT_NOT_NULL(strstr(frame, "\"heart_rate\":72"));
    TEST_ASSERT_NOT_NULL(strstr(frame, "\"finger_detected\":1"));
}

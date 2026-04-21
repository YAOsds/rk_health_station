#include "imu_event_state.h"
#include "unity.h"

void test_imu_event_state_promotes_after_two_consecutive_falls(void)
{
    imu_event_state_t state;
    fall_classifier_result_t result = {
        .label = IMU_FALL_CLASS_FALL,
        .probabilities = {0.02f, 0.08f, 0.90f},
    };

    imu_event_state_init(&state, 2);
    imu_event_state_update(&state, &result);
    TEST_ASSERT_EQUAL(IMU_FALL_CLASS_PRE_IMPACT, imu_event_state_current_label(&state));
    imu_event_state_update(&state, &result);
    TEST_ASSERT_EQUAL(IMU_FALL_CLASS_FALL, imu_event_state_current_label(&state));
}

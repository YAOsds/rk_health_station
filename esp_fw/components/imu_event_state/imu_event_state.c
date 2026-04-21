#include "imu_event_state.h"

#include <string.h>

void imu_event_state_init(imu_event_state_t *state, size_t required_consecutive_falls)
{
    memset(state, 0, sizeof(*state));
    state->required_consecutive_falls = required_consecutive_falls;
    state->current_label = IMU_FALL_CLASS_NON_FALL;
}

void imu_event_state_update(imu_event_state_t *state, const fall_classifier_result_t *result)
{
    state->latest_result = *result;
    if (result->label == IMU_FALL_CLASS_FALL) {
        state->consecutive_fall_windows++;
        state->current_label = state->consecutive_fall_windows >= state->required_consecutive_falls
            ? IMU_FALL_CLASS_FALL
            : IMU_FALL_CLASS_PRE_IMPACT;
        return;
    }
    state->consecutive_fall_windows = 0;
    state->current_label = result->label;
}

imu_fall_class_t imu_event_state_current_label(const imu_event_state_t *state)
{
    return state->current_label;
}

const fall_classifier_result_t *imu_event_state_latest(const imu_event_state_t *state)
{
    return &state->latest_result;
}

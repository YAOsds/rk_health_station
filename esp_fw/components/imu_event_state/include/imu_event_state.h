#pragma once

#include <stddef.h>

#include "fall_classifier.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t consecutive_fall_windows;
    size_t required_consecutive_falls;
    imu_fall_class_t current_label;
    fall_classifier_result_t latest_result;
} imu_event_state_t;

void imu_event_state_init(imu_event_state_t *state, size_t required_consecutive_falls);
void imu_event_state_update(imu_event_state_t *state, const fall_classifier_result_t *result);
imu_fall_class_t imu_event_state_current_label(const imu_event_state_t *state);
const fall_classifier_result_t *imu_event_state_latest(const imu_event_state_t *state);

#ifdef __cplusplus
}
#endif

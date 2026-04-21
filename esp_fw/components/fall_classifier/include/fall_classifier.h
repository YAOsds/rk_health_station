#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IMU_FALL_CLASS_NON_FALL = 0,
    IMU_FALL_CLASS_PRE_IMPACT = 1,
    IMU_FALL_CLASS_FALL = 2,
} imu_fall_class_t;

typedef struct {
    bool valid;
    imu_fall_class_t label;
    float probabilities[3];
} fall_classifier_result_t;

esp_err_t fall_classifier_init(void);
esp_err_t fall_classifier_run(const float window[256][6], fall_classifier_result_t *result);
void fall_classifier_deinit(void);

#ifdef __cplusplus
}
#endif

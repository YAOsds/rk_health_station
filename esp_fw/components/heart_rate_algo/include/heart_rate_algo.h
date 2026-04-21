#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "signal_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int heart_rate_bpm;
    float spo2_percent;
    bool finger_detected;
    float confidence;
    float motion_level;
} heart_rate_result_t;

esp_err_t heart_rate_algo_compute(const signal_quality_t *quality, heart_rate_result_t *result);

#ifdef __cplusplus
}
#endif

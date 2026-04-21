#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIGNAL_FILTER_MAX_SAMPLES 128
#define SIGNAL_FILTER_FINGER_THRESHOLD 7000.0f

typedef struct {
    size_t sample_count;
    uint32_t sample_period_ms;
    uint32_t ir_samples[SIGNAL_FILTER_MAX_SAMPLES];
    uint32_t red_samples[SIGNAL_FILTER_MAX_SAMPLES];
} signal_filter_window_t;

typedef struct {
    bool finger_detected;
    float confidence;
    float dc_ir;
    float dc_red;
    float ac_ir_rms;
    float ac_red_rms;
    float motion_level;
    float estimated_bpm;
    float estimated_spo2;
    uint32_t peak_count;
} signal_quality_t;

esp_err_t signal_filter_analyze_window(const signal_filter_window_t *window, signal_quality_t *quality);
esp_err_t signal_filter_estimate_motion_level(const float *accel_norm_samples,
    size_t sample_count,
    float *motion_level);

#ifdef __cplusplus
}
#endif

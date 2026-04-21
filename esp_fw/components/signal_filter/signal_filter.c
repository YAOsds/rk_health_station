#include "signal_filter.h"

#include <math.h>
#include <string.h>

static float clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint32_t detect_peaks(const signal_filter_window_t *window, float mean_ir, float threshold, float *estimated_bpm)
{
    uint32_t peaks[SIGNAL_FILTER_MAX_SAMPLES] = {0};
    uint32_t peak_count = 0;
    uint32_t min_peak_distance;
    float interval_sum = 0.0f;

    if (window->sample_period_ms == 0) {
        *estimated_bpm = 0.0f;
        return 0;
    }

    min_peak_distance = 300U / window->sample_period_ms;
    if (min_peak_distance == 0) {
        min_peak_distance = 1;
    }

    for (size_t i = 1; i + 1 < window->sample_count; ++i) {
        uint32_t sample = window->ir_samples[i];
        if ((float)sample <= mean_ir + threshold) {
            continue;
        }
        if (sample < window->ir_samples[i - 1] || sample < window->ir_samples[i + 1]) {
            continue;
        }
        if (peak_count > 0 && (uint32_t)i - peaks[peak_count - 1] < min_peak_distance) {
            continue;
        }
        peaks[peak_count++] = (uint32_t)i;
    }

    if (peak_count >= 2) {
        for (uint32_t i = 1; i < peak_count; ++i) {
            interval_sum += (float)(peaks[i] - peaks[i - 1]) * (float)window->sample_period_ms;
        }
        interval_sum /= (float)(peak_count - 1);
        if (interval_sum > 0.0f) {
            *estimated_bpm = 60000.0f / interval_sum;
        }
    } else {
        *estimated_bpm = 0.0f;
    }

    return peak_count;
}

esp_err_t signal_filter_analyze_window(const signal_filter_window_t *window, signal_quality_t *quality)
{
    double sum_ir = 0.0;
    double sum_red = 0.0;
    double rms_ir = 0.0;
    double rms_red = 0.0;
    float ratio;
    float quality_strength;

    if (window == NULL || quality == NULL || window->sample_count == 0
        || window->sample_count > SIGNAL_FILTER_MAX_SAMPLES || window->sample_period_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(quality, 0, sizeof(*quality));

    for (size_t i = 0; i < window->sample_count; ++i) {
        sum_ir += window->ir_samples[i];
        sum_red += window->red_samples[i];
    }

    quality->dc_ir = (float)(sum_ir / (double)window->sample_count);
    quality->dc_red = (float)(sum_red / (double)window->sample_count);
    quality->finger_detected = quality->dc_ir >= SIGNAL_FILTER_FINGER_THRESHOLD;

    for (size_t i = 0; i < window->sample_count; ++i) {
        float ir_delta = (float)window->ir_samples[i] - quality->dc_ir;
        float red_delta = (float)window->red_samples[i] - quality->dc_red;

        rms_ir += (double)(ir_delta * ir_delta);
        rms_red += (double)(red_delta * red_delta);
    }

    quality->ac_ir_rms = sqrtf((float)(rms_ir / (double)window->sample_count));
    quality->ac_red_rms = sqrtf((float)(rms_red / (double)window->sample_count));
    quality->peak_count = detect_peaks(window, quality->dc_ir, quality->ac_ir_rms * 0.5f, &quality->estimated_bpm);

    if (quality->dc_ir > 0.0f && quality->dc_red > 0.0f && quality->ac_ir_rms > 0.0f && quality->ac_red_rms > 0.0f) {
        ratio = (quality->ac_red_rms / quality->dc_red) / (quality->ac_ir_rms / quality->dc_ir);
        quality->estimated_spo2 = clampf(120.0f - 25.0f * ratio, 70.0f, 100.0f);
    }

    quality_strength = quality->dc_ir > 0.0f ? (quality->ac_ir_rms / quality->dc_ir) * 40.0f : 0.0f;
    quality->confidence = clampf((quality->finger_detected ? 0.25f : 0.0f)
            + clampf(quality_strength, 0.0f, 0.45f)
            + (quality->peak_count >= 2 ? 0.30f : 0.0f),
        0.0f,
        1.0f);
    return ESP_OK;
}

esp_err_t signal_filter_estimate_motion_level(const float *accel_norm_samples,
    size_t sample_count,
    float *motion_level)
{
    double mean = 0.0;
    double variance = 0.0;

    if (accel_norm_samples == NULL || motion_level == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < sample_count; ++i) {
        mean += accel_norm_samples[i];
    }
    mean /= (double)sample_count;

    for (size_t i = 0; i < sample_count; ++i) {
        double delta = accel_norm_samples[i] - mean;
        variance += delta * delta;
    }

    *motion_level = sqrtf((float)(variance / (double)sample_count));
    return ESP_OK;
}

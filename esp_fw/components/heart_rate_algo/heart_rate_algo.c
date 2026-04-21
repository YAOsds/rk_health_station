#include "heart_rate_algo.h"

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

esp_err_t heart_rate_algo_compute(const signal_quality_t *quality, heart_rate_result_t *result)
{
    if (quality == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!quality->finger_detected || quality->confidence < 0.25f) {
        memset(result, 0, sizeof(*result));
        return ESP_ERR_INVALID_STATE;
    }

    result->heart_rate_bpm = (int)lroundf(clampf(quality->estimated_bpm, 0.0f, 220.0f));
    result->spo2_percent = clampf(quality->estimated_spo2, 70.0f, 100.0f);
    result->finger_detected = quality->finger_detected;
    result->confidence = quality->confidence;
    result->motion_level = quality->motion_level;
    return ESP_OK;
}

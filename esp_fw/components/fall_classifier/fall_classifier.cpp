#include "fall_classifier.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "dl_model_base.hpp"
#include "dl_tensor_base.hpp"
#include "esp_log.h"

extern "C" {
extern const uint8_t _binary_imu_fall_waist_3class_espdl_start[];
extern const uint8_t _binary_imu_fall_waist_3class_espdl_end[];
}

namespace {
constexpr int kWindowSamples = 256;
constexpr int kWindowAxes = 6;
constexpr int kNumClasses = 3;

static const char *TAG = "FALL_CLS";

struct FallClassifierRuntime {
    dl::Model *model = nullptr;
    bool useHeuristic = true;
};

FallClassifierRuntime g_runtime;

void softmax3(const float logits[kNumClasses], float probs[kNumClasses])
{
    float maxLogit = logits[0];
    for (int i = 1; i < kNumClasses; ++i) {
        if (logits[i] > maxLogit) {
            maxLogit = logits[i];
        }
    }

    float sum = 0.0f;
    for (int i = 0; i < kNumClasses; ++i) {
        probs[i] = expf(logits[i] - maxLogit);
        sum += probs[i];
    }
    for (int i = 0; i < kNumClasses; ++i) {
        probs[i] /= sum;
    }
}

void runHeuristic(const float window[kWindowSamples][kWindowAxes], fall_classifier_result_t *result)
{
    float accelEnergy = 0.0f;
    float gyroEnergy = 0.0f;
    float logits[kNumClasses] = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < kWindowSamples; ++i) {
        accelEnergy += fabsf(window[i][0]) + fabsf(window[i][1]) + fabsf(window[i][2]);
        gyroEnergy += fabsf(window[i][3]) + fabsf(window[i][4]) + fabsf(window[i][5]);
    }

    accelEnergy /= static_cast<float>(kWindowSamples);
    gyroEnergy /= static_cast<float>(kWindowSamples);
    logits[0] = 2.0f - 0.20f * accelEnergy;
    logits[1] = 0.12f * accelEnergy + 0.02f * gyroEnergy;
    logits[2] = 0.10f * accelEnergy + 0.05f * gyroEnergy - 0.5f;

    softmax3(logits, result->probabilities);
}

imu_fall_class_t argmax3(const float probs[kNumClasses])
{
    int best = 0;
    for (int i = 1; i < kNumClasses; ++i) {
        if (probs[i] > probs[best]) {
            best = i;
        }
    }
    return static_cast<imu_fall_class_t>(best);
}

float readOutputValue(dl::TensorBase *output, int index)
{
    switch (output->dtype) {
    case dl::DATA_TYPE_FLOAT:
        return output->get_element<float>(index);
    case dl::DATA_TYPE_INT8:
        return dl::dequantize<int8_t, float>(output->get_element<int8_t>(index), DL_SCALE(output->exponent));
    case dl::DATA_TYPE_INT16:
        return dl::dequantize<int16_t, float>(output->get_element<int16_t>(index), DL_SCALE(output->exponent));
    default:
        ESP_LOGW(TAG, "unsupported output dtype=%s, falling back to zero", output->get_dtype_string());
        return 0.0f;
    }
}

bool tryRunModel(const float window[kWindowSamples][kWindowAxes], fall_classifier_result_t *result)
{
    if (!g_runtime.model) {
        return false;
    }

    dl::TensorBase inputTensor({1, 1, kWindowSamples, kWindowAxes}, window, 0, dl::DATA_TYPE_FLOAT, false);
    g_runtime.model->run(&inputTensor);

    dl::TensorBase *output = g_runtime.model->get_output();
    if (!output || output->get_size() < kNumClasses) {
        ESP_LOGW(TAG, "model output is unavailable, falling back to heuristic");
        return false;
    }

    float logits[kNumClasses] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < kNumClasses; ++i) {
        logits[i] = readOutputValue(output, i);
    }
    softmax3(logits, result->probabilities);
    return true;
}
}

extern "C" esp_err_t fall_classifier_init(void)
{
    if (g_runtime.model) {
        return ESP_OK;
    }

    g_runtime.useHeuristic = true;
    const size_t modelSize = static_cast<size_t>(_binary_imu_fall_waist_3class_espdl_end
        - _binary_imu_fall_waist_3class_espdl_start);
    if (modelSize == 0U) {
        ESP_LOGW(TAG, "embedded espdl artifact is empty, using heuristic fallback");
        return ESP_OK;
    }

    g_runtime.model = new (std::nothrow) dl::Model(
        reinterpret_cast<const char *>(_binary_imu_fall_waist_3class_espdl_start),
        fbs::MODEL_LOCATION_IN_FLASH_RODATA,
        0,
        dl::MEMORY_MANAGER_GREEDY,
        nullptr,
        true);
    if (!g_runtime.model || g_runtime.model->get_inputs().empty() || g_runtime.model->get_outputs().empty()) {
        ESP_LOGW(TAG, "failed to load esp-dl model, using heuristic fallback");
        delete g_runtime.model;
        g_runtime.model = nullptr;
        return ESP_OK;
    }

    g_runtime.useHeuristic = false;
    ESP_LOGI(TAG, "esp-dl model loaded successfully");
    return ESP_OK;
}

extern "C" esp_err_t fall_classifier_run(const float window[kWindowSamples][kWindowAxes],
    fall_classifier_result_t *result)
{
    if (!result) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    if (g_runtime.useHeuristic) {
        runHeuristic(window, result);
    } else if (!tryRunModel(window, result)) {
        runHeuristic(window, result);
    }

    result->valid = true;
    result->label = argmax3(result->probabilities);
    return ESP_OK;
}

extern "C" void fall_classifier_deinit(void)
{
    delete g_runtime.model;
    g_runtime.model = nullptr;
    g_runtime.useHeuristic = true;
}

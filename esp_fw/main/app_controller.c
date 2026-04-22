#include "app_controller.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_events.h"
#include "auth_client.h"
#include "board_config.h"
#include "device_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fall_classifier.h"
#include "heart_rate_algo.h"
#include "imu_event_state.h"
#include "imu_window_buffer.h"
#include "led_status.h"
#include "max30102.h"
#include "mpu6050.h"
#include "nvs_flash.h"
#include "provisioning_portal.h"
#include "signal_filter.h"
#include "system_diag.h"
#include "telemetry_uploader.h"
#include "wifi_manager.h"

#define MOTION_HISTORY_CAPACITY 64
#define SENSOR_INIT_RETRY_MS 2000
#define LINK_RETRY_MS 5000
#define PPG_MIN_ANALYSIS_SAMPLES 64

static const char *TAG = "APP_CTRL";
static const char *APP_FIRMWARE_VERSION = "0.1.0-dev";
static bool s_sensor_bus_ready = false;

typedef struct {
    EventGroupHandle_t events;
    SemaphoreHandle_t mutex;
    rk_device_config_t config;
    telemetry_vitals_t latest_vitals;
    signal_quality_t latest_quality;
    bool vitals_valid;
} app_runtime_t;

typedef struct {
    uint32_t ir_samples[SIGNAL_FILTER_MAX_SAMPLES];
    uint32_t red_samples[SIGNAL_FILTER_MAX_SAMPLES];
    float accel_norm_samples[MOTION_HISTORY_CAPACITY];
    float imu_window[256][6];
    signal_filter_window_t filter_window;
} sensor_task_buffers_t;

static app_runtime_t s_runtime;

static int64_t monotonic_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static esp_err_t sensor_bus_init(void)
{
    const board_config_t *board = board_config_get();
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = board->i2c_sda_pin,
        .scl_io_num = board->i2c_scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t ret;

    if (s_sensor_bus_ready) {
        return ESP_OK;
    }

    ret = i2c_param_config(board->i2c_port, &i2c_conf);

    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_driver_install(board->i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    s_sensor_bus_ready = true;
    ESP_LOGI(TAG, "sensor bus ready on i2c=%d sda=%d scl=%d",
        (int)board->i2c_port,
        (int)board->i2c_sda_pin,
        (int)board->i2c_scl_pin);
    return ESP_OK;
}

static esp_err_t init_nvs_flash(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

static void append_float(float *buffer, size_t capacity, size_t *count, float value)
{
    if (*count < capacity) {
        buffer[*count] = value;
        ++(*count);
        return;
    }

    memmove(buffer, buffer + 1, (capacity - 1) * sizeof(*buffer));
    buffer[capacity - 1] = value;
}

static void publish_vitals(const telemetry_vitals_t *vitals, const signal_quality_t *quality)
{
    xSemaphoreTake(s_runtime.mutex, portMAX_DELAY);
    s_runtime.latest_vitals = *vitals;
    s_runtime.latest_quality = *quality;
    s_runtime.vitals_valid = true;
    xSemaphoreGive(s_runtime.mutex);

    xEventGroupSetBits(s_runtime.events, APP_EVENT_VITALS_READY);
}

static bool snapshot_vitals(telemetry_vitals_t *vitals, signal_quality_t *quality)
{
    bool valid;

    xSemaphoreTake(s_runtime.mutex, portMAX_DELAY);
    valid = s_runtime.vitals_valid;
    if (valid) {
        *vitals = s_runtime.latest_vitals;
        *quality = s_runtime.latest_quality;
    }
    xSemaphoreGive(s_runtime.mutex);
    return valid;
}

static void sensor_task(void *arg)
{
    const board_config_t *board = board_config_get();
    const uint32_t ppg_period_ms = board->max30102_period_ms < 10 ? 10 : board->max30102_period_ms;
    sensor_task_buffers_t *buffers = calloc(1, sizeof(*buffers));
    max30102_config_t max_cfg = {
        .i2c_port = board->i2c_port,
        .bus_speed_hz = 400000,
        .red_led_amplitude = 0x24,
        .ir_led_amplitude = 0x24,
    };
    mpu6050_config_t mpu_cfg = {
        .i2c_port = board->i2c_port,
        .bus_speed_hz = 400000,
        .accel_range = MPU6050_ACCEL_RANGE_4G,
        .gyro_range = MPU6050_GYRO_RANGE_500,
        .bandwidth = MPU6050_BAND_21_HZ,
    };
    max30102_handle_t max_handle = NULL;
    mpu6050_handle_t mpu_handle = NULL;
    size_t ppg_count = 0;
    size_t motion_count = 0;
    int64_t next_telemetry_ms = monotonic_ms() + (int64_t)board->telemetry_period_ms;
    int64_t next_mpu_ms = monotonic_ms();
    imu_window_buffer_t imu_buffer = {0};
    imu_event_state_t imu_state = {0};
    telemetry_imu_fall_t latest_imu_fall = {0};
    bool fall_classifier_ready = false;

    (void)arg;

    if (buffers == NULL) {
        xEventGroupSetBits(s_runtime.events, APP_EVENT_SENSOR_FAULT);
        ESP_LOGE(TAG, "sensor task buffer allocation failed");
        vTaskDelete(NULL);
        return;
    }

    imu_window_buffer_init(&imu_buffer, 256, 32);
    imu_event_state_init(&imu_state, 2);

    while (true) {
        max30102_sample_t ppg_sample = {0};
        signal_quality_t quality = {0};
        heart_rate_result_t hr_result = {0};
        telemetry_vitals_t vitals = {0};
        mpu6050_sample_t motion_sample = {0};
        int64_t now_ms = monotonic_ms();

        if ((max_handle == NULL || mpu_handle == NULL) && sensor_bus_init() != ESP_OK) {
            xEventGroupSetBits(s_runtime.events, APP_EVENT_SENSOR_FAULT);
            ESP_LOGW(TAG, "sensor bus init failed, retry in %d ms", SENSOR_INIT_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(SENSOR_INIT_RETRY_MS));
            continue;
        }

        if (max_handle == NULL) {
            esp_err_t ret = max30102_init(&max_cfg, &max_handle);
            if (ret == ESP_OK) {
                ret = max30102_configure(max_handle);
            }
            if (ret != ESP_OK) {
                if (max_handle != NULL) {
                    max30102_deinit(max_handle);
                    max_handle = NULL;
                }
                xEventGroupSetBits(s_runtime.events, APP_EVENT_SENSOR_FAULT);
                ESP_LOGW(TAG,
                    "max30102 init/configure failed: %s, retry in %d ms",
                    esp_err_to_name(ret),
                    SENSOR_INIT_RETRY_MS);
                vTaskDelay(pdMS_TO_TICKS(SENSOR_INIT_RETRY_MS));
                continue;
            }
            ESP_LOGI(TAG, "max30102 streaming at %lu ms/sample", (unsigned long)ppg_period_ms);
        }

        if (mpu_handle == NULL) {
            esp_err_t ret = mpu6050_init(&mpu_cfg, &mpu_handle);
            if (ret != ESP_OK) {
                if (mpu_handle != NULL) {
                    mpu6050_deinit(mpu_handle);
                    mpu_handle = NULL;
                }
                xEventGroupSetBits(s_runtime.events, APP_EVENT_SENSOR_FAULT);
                ESP_LOGW(TAG,
                    "mpu6050 init failed: %s, retry in %d ms",
                    esp_err_to_name(ret),
                    SENSOR_INIT_RETRY_MS);
                vTaskDelay(pdMS_TO_TICKS(SENSOR_INIT_RETRY_MS));
                continue;
            }
            ESP_LOGI(TAG, "mpu6050 streaming at %lu ms/sample", (unsigned long)board->mpu6050_period_ms);
            if (!fall_classifier_ready) {
                if (fall_classifier_init() == ESP_OK) {
                    fall_classifier_ready = true;
                } else {
                    ESP_LOGW(TAG, "fall classifier init failed, IMU model disabled");
                }
            }
        }

        if (max30102_read_sample(max_handle, &ppg_sample) != ESP_OK) {
            ESP_LOGW(TAG, "max30102 read failed, reinitializing");
            max30102_deinit(max_handle);
            max_handle = NULL;
            continue;
        }
        if (ppg_count < SIGNAL_FILTER_MAX_SAMPLES) {
            buffers->ir_samples[ppg_count] = ppg_sample.ir;
            buffers->red_samples[ppg_count] = ppg_sample.red;
            ++ppg_count;
        } else {
            memmove(buffers->ir_samples, buffers->ir_samples + 1,
                (SIGNAL_FILTER_MAX_SAMPLES - 1) * sizeof(buffers->ir_samples[0]));
            memmove(buffers->red_samples, buffers->red_samples + 1,
                (SIGNAL_FILTER_MAX_SAMPLES - 1) * sizeof(buffers->red_samples[0]));
            buffers->ir_samples[SIGNAL_FILTER_MAX_SAMPLES - 1] = ppg_sample.ir;
            buffers->red_samples[SIGNAL_FILTER_MAX_SAMPLES - 1] = ppg_sample.red;
        }

        if (now_ms >= next_mpu_ms) {
            if (mpu6050_read_sample(mpu_handle, &motion_sample) != ESP_OK) {
                ESP_LOGW(TAG, "mpu6050 read failed, reinitializing");
                mpu6050_deinit(mpu_handle);
                mpu_handle = NULL;
                continue;
            }
            append_float(
                buffers->accel_norm_samples, MOTION_HISTORY_CAPACITY, &motion_count, motion_sample.accel_norm_g);
            if (fall_classifier_ready) {
                imu_sample6_t imu_sample = {
                    .values = {
                        motion_sample.accel_x_g,
                        motion_sample.accel_y_g,
                        motion_sample.accel_z_g,
                        motion_sample.gyro_x_dps,
                        motion_sample.gyro_y_dps,
                        motion_sample.gyro_z_dps,
                    },
                };
                if (imu_window_buffer_push(&imu_buffer, &imu_sample)) {
                    fall_classifier_result_t cls = {0};
                    imu_window_buffer_copy_latest(&imu_buffer, buffers->imu_window);
                    if (fall_classifier_run(buffers->imu_window, &cls) == ESP_OK) {
                        imu_event_state_update(&imu_state, &cls);
                        latest_imu_fall.valid = true;
                        latest_imu_fall.label = imu_event_state_current_label(&imu_state);
                        latest_imu_fall.non_fall_prob = cls.probabilities[0];
                        latest_imu_fall.pre_impact_prob = cls.probabilities[1];
                        latest_imu_fall.fall_prob = cls.probabilities[2];
                        ESP_LOGI(TAG,
                            "imu_fall class=%d probs=[%.3f %.3f %.3f]",
                            (int)latest_imu_fall.label,
                            (double)latest_imu_fall.non_fall_prob,
                            (double)latest_imu_fall.pre_impact_prob,
                            (double)latest_imu_fall.fall_prob);
                    }
                }
            }
            next_mpu_ms = now_ms + (int64_t)board->mpu6050_period_ms;
        }

        if (now_ms >= next_telemetry_ms && ppg_count >= PPG_MIN_ANALYSIS_SAMPLES) {
            memset(&buffers->filter_window, 0, sizeof(buffers->filter_window));
            buffers->filter_window.sample_count = ppg_count;
            buffers->filter_window.sample_period_ms = ppg_period_ms;
            memcpy(buffers->filter_window.ir_samples, buffers->ir_samples,
                ppg_count * sizeof(buffers->ir_samples[0]));
            memcpy(buffers->filter_window.red_samples, buffers->red_samples,
                ppg_count * sizeof(buffers->red_samples[0]));

            if (signal_filter_analyze_window(&buffers->filter_window, &quality) == ESP_OK) {
                if (motion_count > 0) {
                    signal_filter_estimate_motion_level(
                        buffers->accel_norm_samples, motion_count, &quality.motion_level);
                }
                system_diag_note_signal_quality(quality.confidence, quality.finger_detected, quality.motion_level);
            }

            vitals.acceleration = quality.motion_level;
            vitals.finger_detected = quality.finger_detected;
            vitals.imu_fall = latest_imu_fall;
            if (heart_rate_algo_compute(&quality, &hr_result) == ESP_OK) {
                vitals.heart_rate = hr_result.heart_rate_bpm;
                vitals.spo2 = hr_result.spo2_percent;
                vitals.finger_detected = hr_result.finger_detected;
            } else {
                vitals.heart_rate = 0;
                vitals.spo2 = 0.0f;
            }

            publish_vitals(&vitals, &quality);
            xEventGroupClearBits(s_runtime.events, APP_EVENT_SENSOR_FAULT);
            next_telemetry_ms = now_ms + (int64_t)board->telemetry_period_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(ppg_period_ms));
    }
}

static void link_task(void *arg)
{
    (void)arg;

    while (true) {
        system_diag_set_stage(SYSTEM_DIAG_STAGE_WIFI);
        led_status_set(LED_STATUS_WIFI_CONNECTING);

        if (wifi_manager_start(&s_runtime.config) != ESP_OK || !wifi_manager_is_connected()) {
            system_diag_note_wifi_retry();
            ESP_LOGW(TAG, "wifi connection not ready, retry in %d ms", LINK_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(LINK_RETRY_MS));
            continue;
        }

        ESP_LOGI(TAG,
            "wifi connected ip=%s rssi=%d, target=%s:%d",
            wifi_manager_get_ip(),
            wifi_manager_get_rssi(),
            s_runtime.config.server_ip,
            s_runtime.config.server_port);

        if (telemetry_uploader_init(&s_runtime.config, APP_FIRMWARE_VERSION) != ESP_OK) {
            ESP_LOGW(TAG, "telemetry uploader init failed, retry in %d ms", LINK_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(LINK_RETRY_MS));
            continue;
        }

        xEventGroupSetBits(s_runtime.events, APP_EVENT_LINK_READY);

        while (wifi_manager_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        xEventGroupClearBits(s_runtime.events, APP_EVENT_LINK_READY);
        telemetry_uploader_deinit();
        wifi_manager_stop();
        ESP_LOGW(TAG, "wifi link dropped, restarting connection loop");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void telemetry_task(void *arg)
{
    const board_config_t *board = board_config_get();
    uint32_t seq = 1;
    char frame[768];

    (void)arg;

    while (true) {
        EventBits_t bits = xEventGroupWaitBits(s_runtime.events,
            APP_EVENT_VITALS_READY | APP_EVENT_LINK_READY,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(board->telemetry_period_ms));
        telemetry_vitals_t vitals = {0};
        signal_quality_t quality = {0};
        auth_client_state_t auth_state = AUTH_STATE_UNAUTHENTICATED;
        esp_err_t ret;

        if ((bits & (APP_EVENT_VITALS_READY | APP_EVENT_LINK_READY))
            != (APP_EVENT_VITALS_READY | APP_EVENT_LINK_READY)) {
            continue;
        }
        if (!snapshot_vitals(&vitals, &quality)) {
            continue;
        }

        ret = telemetry_uploader_build_frame(s_runtime.config.device_id,
            s_runtime.config.device_name,
            APP_FIRMWARE_VERSION,
            seq,
            esp_timer_get_time() / 1000000,
            &vitals,
            frame,
            sizeof(frame));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "telemetry frame build failed: %s", esp_err_to_name(ret));
            continue;
        }

        ret = telemetry_uploader_send_json(frame, strlen(frame), &auth_state);
        if (ret == ESP_OK && auth_state == AUTH_STATE_AUTHENTICATED) {
            system_diag_set_stage(SYSTEM_DIAG_STAGE_STREAMING);
            led_status_set(LED_STATUS_STREAMING);
            xEventGroupClearBits(s_runtime.events, APP_EVENT_PENDING_APPROVAL | APP_EVENT_REJECTED);
            ESP_LOGI(TAG,
                "telemetry sent hr=%d spo2=%.1f accel=%.3f finger=%d conf=%.2f",
                vitals.heart_rate,
                (double)vitals.spo2,
                (double)vitals.acceleration,
                vitals.finger_detected ? 1 : 0,
                (double)quality.confidence);
            ++seq;
            continue;
        }

        system_diag_set_stage(SYSTEM_DIAG_STAGE_AUTH);
        if (auth_state == AUTH_STATE_PENDING) {
            led_status_set(LED_STATUS_PENDING_APPROVAL);
            xEventGroupSetBits(s_runtime.events, APP_EVENT_PENDING_APPROVAL);
            ESP_LOGW(TAG, "device pending approval on RK3588 host");
        } else if (auth_state == AUTH_STATE_REJECTED) {
            led_status_set(LED_STATUS_FAULT);
            system_diag_set_stage(SYSTEM_DIAG_STAGE_FAULT);
            xEventGroupSetBits(s_runtime.events, APP_EVENT_REJECTED);
            system_diag_note_auth_failure("device rejected by RK3588 host");
            ESP_LOGW(TAG, "device rejected by RK3588 host");
        } else {
            system_diag_note_auth_failure("telemetry send failed");
            ESP_LOGW(TAG, "telemetry/auth send failed: %s state=%d", esp_err_to_name(ret), (int)auth_state);
        }

        vTaskDelay(pdMS_TO_TICKS(LINK_RETRY_MS));
    }
}

void app_controller_run(void)
{
    rk_device_config_t config = {0};

    board_config_init();
    system_diag_init();
    system_diag_set_stage(SYSTEM_DIAG_STAGE_BOOT);
    ESP_ERROR_CHECK(init_nvs_flash());
    ESP_ERROR_CHECK(led_status_init());
    ESP_ERROR_CHECK(led_status_set(LED_STATUS_BOOT));

    if (rk_device_config_load(&config) != ESP_OK || !rk_device_config_is_complete(&config)) {
        system_diag_set_stage(SYSTEM_DIAG_STAGE_PROVISIONING);
        led_status_set(LED_STATUS_PROVISIONING);
        ESP_LOGW(TAG, "device config missing, entering provisioning portal");
        ESP_ERROR_CHECK(provisioning_portal_start());
        return;
    }

    memset(&s_runtime, 0, sizeof(s_runtime));
    memcpy(&s_runtime.config, &config, sizeof(config));
    s_runtime.events = xEventGroupCreate();
    s_runtime.mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_runtime.events != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(s_runtime.mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5, NULL);
    xTaskCreate(link_task, "link_task", 5120, NULL, 6, NULL);
    xTaskCreate(telemetry_task, "telemetry_task", 5120, NULL, 5, NULL);
    ESP_LOGI(TAG, "runtime tasks started");
}

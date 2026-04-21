#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "auth_client.h"
#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int heart_rate;
    float spo2;
    float acceleration;
    bool finger_detected;
} telemetry_vitals_t;

esp_err_t telemetry_uploader_init(const rk_device_config_t *config, const char *firmware_version);
void telemetry_uploader_deinit(void);
esp_err_t telemetry_uploader_build_frame(const char *device_id, const char *device_name,
    const char *firmware_version, uint32_t seq, int64_t ts,
    const telemetry_vitals_t *vitals, char *buffer, size_t buffer_len);
esp_err_t telemetry_uploader_send_json(const char *json_frame, size_t len, auth_client_state_t *state);

#ifdef __cplusplus
}
#endif

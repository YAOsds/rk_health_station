#pragma once

#include <stddef.h>

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t telemetry_uploader_init(const rk_device_config_t *config, const char *firmware_version);
void telemetry_uploader_deinit(void);
esp_err_t telemetry_uploader_send_json(const char *json_frame, size_t len, auth_client_result_t *result);

#ifdef __cplusplus
}
#endif

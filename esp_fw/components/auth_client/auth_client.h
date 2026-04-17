#pragma once

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t auth_client_authenticate(
    const rk_device_config_t *config,
    const char *firmware_version,
    auth_client_result_t *result);

#ifdef __cplusplus
}
#endif

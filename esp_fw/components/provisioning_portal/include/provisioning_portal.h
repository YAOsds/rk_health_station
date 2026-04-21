#pragma once

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t provisioning_portal_start(void);
esp_err_t provisioning_portal_stop(void);
esp_err_t provisioning_portal_parse_form(const char *body, rk_device_config_t *config);

#ifdef __cplusplus
}
#endif

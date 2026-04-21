#pragma once

#include <stdbool.h>

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_manager_start(const rk_device_config_t *config);
esp_err_t wifi_manager_stop(void);
bool wifi_manager_is_connected(void);
int wifi_manager_get_rssi(void);
const char *wifi_manager_get_ip(void);

#ifdef __cplusplus
}
#endif

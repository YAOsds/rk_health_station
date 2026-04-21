#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RK_WIFI_SSID_LEN 64
#define RK_WIFI_PASSWORD_LEN 64
#define RK_SERVER_IP_LEN 64
#define RK_DEVICE_ID_LEN 64
#define RK_DEVICE_NAME_LEN 64
#define RK_DEVICE_SECRET_LEN 128

typedef struct {
    char wifi_ssid[RK_WIFI_SSID_LEN];
    char wifi_password[RK_WIFI_PASSWORD_LEN];
    char server_ip[RK_SERVER_IP_LEN];
    int server_port;
    char device_id[RK_DEVICE_ID_LEN];
    char device_name[RK_DEVICE_NAME_LEN];
    char device_secret[RK_DEVICE_SECRET_LEN];
} rk_device_config_t;

typedef enum {
    AUTH_OK = 0,
    AUTH_PENDING,
    AUTH_REJECTED,
    AUTH_RETRY,
} auth_client_result_t;

void rk_device_config_clear(rk_device_config_t *config);
bool rk_device_config_is_complete(const rk_device_config_t *config);
esp_err_t rk_device_config_load(rk_device_config_t *config);
esp_err_t rk_device_config_save(const rk_device_config_t *config);

#ifdef __cplusplus
}
#endif

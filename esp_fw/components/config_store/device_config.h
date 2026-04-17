#pragma once

#include <stdint.h>

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

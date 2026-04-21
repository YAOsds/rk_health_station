#include <string.h>

#include "unity.h"

#include "device_config.h"

void test_device_config_requires_all_fields(void)
{
    rk_device_config_t config = {0};

    strcpy(config.wifi_ssid, "lab-ap");
    strcpy(config.device_id, "watch_001");
    TEST_ASSERT_FALSE(rk_device_config_is_complete(&config));
}

void test_device_config_accepts_complete_form(void)
{
    rk_device_config_t config = {0};

    strcpy(config.wifi_ssid, "lab-ap");
    strcpy(config.wifi_password, "12345678");
    strcpy(config.server_ip, "192.168.137.1");
    config.server_port = 19001;
    strcpy(config.device_id, "watch_001");
    strcpy(config.device_name, "RK Watch 01");
    strcpy(config.device_secret, "topsecret");
    TEST_ASSERT_TRUE(rk_device_config_is_complete(&config));
}

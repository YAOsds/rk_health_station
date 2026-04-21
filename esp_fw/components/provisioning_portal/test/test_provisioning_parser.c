#include "unity.h"

#include "provisioning_portal.h"

void test_parse_form_body_maps_required_fields(void)
{
    const char *body = "wifi_ssid=lab-ap&wifi_password=12345678&server_ip=192.168.137.1&server_port=19001&device_id=watch_001&device_name=RK+Watch+01&device_secret=topsecret";
    rk_device_config_t config = {0};

    TEST_ASSERT_EQUAL(ESP_OK, provisioning_portal_parse_form(body, &config));
    TEST_ASSERT_EQUAL_STRING("RK Watch 01", config.device_name);
    TEST_ASSERT_EQUAL(19001, config.server_port);
}

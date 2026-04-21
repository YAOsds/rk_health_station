#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "provisioning_scan.h"

static void test_normalize_scan_results(void)
{
    rk_provisioning_scan_ap_t input[] = {
        { .ssid = "Office", .rssi = -72 },
        { .ssid = "Lab", .rssi = -48 },
        { .ssid = "Office", .rssi = -55 },
        { .ssid = "", .rssi = -10 },
        { .ssid = "Guest", .rssi = -81 },
    };
    rk_provisioning_scan_ap_t output[5] = {0};

    size_t count = rk_provisioning_normalize_scan_results(input, 5, output, 5);

    assert(count == 3);
    assert(strcmp(output[0].ssid, "Lab") == 0);
    assert(output[0].rssi == -48);
    assert(strcmp(output[1].ssid, "Office") == 0);
    assert(output[1].rssi == -55);
    assert(strcmp(output[2].ssid, "Guest") == 0);
    assert(output[2].rssi == -81);
}

static void test_build_scan_json(void)
{
    rk_provisioning_scan_ap_t aps[] = {
        { .ssid = "Lab", .rssi = -48 },
        { .ssid = "Office\\2G", .rssi = -55 },
    };
    char json[160] = {0};
    bool ok = rk_provisioning_build_scan_json(aps, 2, json, sizeof(json));

    assert(ok);
    assert(strcmp(json,
        "[{\"ssid\":\"Lab\",\"rssi\":-48},"
        "{\"ssid\":\"Office\\\\2G\",\"rssi\":-55}]") == 0);
}

int main(void)
{
    test_normalize_scan_results();
    test_build_scan_json();
    return 0;
}

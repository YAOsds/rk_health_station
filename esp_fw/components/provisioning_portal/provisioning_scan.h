#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RK_PROVISIONING_SCAN_SSID_LEN 33

typedef struct {
    char ssid[RK_PROVISIONING_SCAN_SSID_LEN];
    int rssi;
} rk_provisioning_scan_ap_t;

size_t rk_provisioning_normalize_scan_results(
    const rk_provisioning_scan_ap_t *input,
    size_t input_count,
    rk_provisioning_scan_ap_t *output,
    size_t output_capacity);

bool rk_provisioning_build_scan_json(
    const rk_provisioning_scan_ap_t *aps,
    size_t ap_count,
    char *json,
    size_t json_size);

#ifdef __cplusplus
}
#endif

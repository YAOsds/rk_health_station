#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "provisioning_scan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RK_PROVISIONING_HEARTBEAT_INTERVAL_MS 3000

typedef struct {
    size_t connected_clients;
    size_t last_scan_count;
    int last_scan_best_rssi;
    char last_scan_best_ssid[RK_PROVISIONING_SCAN_SSID_LEN];
} rk_provisioning_status_t;

bool rk_provisioning_format_heartbeat(
    const rk_provisioning_status_t *status,
    char *buffer,
    size_t buffer_size);

#ifdef __cplusplus
}
#endif

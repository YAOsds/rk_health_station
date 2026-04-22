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

void rk_provisioning_status_note_client_event(rk_provisioning_status_t *status, bool connected);
void rk_provisioning_status_note_scan(
    rk_provisioning_status_t *status,
    const rk_provisioning_scan_ap_t *aps,
    size_t ap_count);

bool rk_provisioning_format_heartbeat(
    const rk_provisioning_status_t *status,
    char *buffer,
    size_t buffer_size);

#ifdef __cplusplus
}
#endif

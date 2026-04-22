#include "provisioning_status.h"

#include <stdio.h>
#include <string.h>

void rk_provisioning_status_note_client_event(rk_provisioning_status_t *status, bool connected)
{
    if (status == NULL) {
        return;
    }

    if (connected) {
        ++status->connected_clients;
    } else if (status->connected_clients > 0) {
        --status->connected_clients;
    }
}

void rk_provisioning_status_note_scan(
    rk_provisioning_status_t *status,
    const rk_provisioning_scan_ap_t *aps,
    size_t ap_count)
{
    if (status == NULL) {
        return;
    }

    status->last_scan_count = ap_count;
    if (aps != NULL && ap_count > 0) {
        strncpy(status->last_scan_best_ssid, aps[0].ssid, sizeof(status->last_scan_best_ssid) - 1);
        status->last_scan_best_ssid[sizeof(status->last_scan_best_ssid) - 1] = '\0';
        status->last_scan_best_rssi = aps[0].rssi;
    } else {
        status->last_scan_best_ssid[0] = '\0';
        status->last_scan_best_rssi = 0;
    }
}

bool rk_provisioning_format_heartbeat(
    const rk_provisioning_status_t *status,
    char *buffer,
    size_t buffer_size)
{
    int written;

    if (status == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    if (status->last_scan_best_ssid[0] != '\0') {
        written = snprintf(buffer, buffer_size,
            "heartbeat clients=%u last_scan_count=%u strongest=%s rssi=%d",
            (unsigned)status->connected_clients,
            (unsigned)status->last_scan_count,
            status->last_scan_best_ssid,
            status->last_scan_best_rssi);
    } else {
        written = snprintf(buffer, buffer_size,
            "heartbeat clients=%u last_scan_count=%u strongest=none",
            (unsigned)status->connected_clients,
            (unsigned)status->last_scan_count);
    }

    return written >= 0 && (size_t)written < buffer_size;
}

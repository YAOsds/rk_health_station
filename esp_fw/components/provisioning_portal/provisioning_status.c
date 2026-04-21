#include "provisioning_status.h"

#include <stdio.h>

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

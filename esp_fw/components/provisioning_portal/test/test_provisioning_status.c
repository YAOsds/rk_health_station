#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "provisioning_status.h"

static void test_note_client_event_saturates_disconnect(void)
{
    rk_provisioning_status_t status = {0};

    rk_provisioning_status_note_client_event(&status, true);
    rk_provisioning_status_note_client_event(&status, true);
    rk_provisioning_status_note_client_event(&status, false);
    rk_provisioning_status_note_client_event(&status, false);
    rk_provisioning_status_note_client_event(&status, false);

    assert(status.connected_clients == 0);
}

static void test_note_scan_updates_summary(void)
{
    rk_provisioning_status_t status = {0};
    rk_provisioning_scan_ap_t aps[] = {
        { .ssid = "Office", .rssi = -48 },
        { .ssid = "Guest", .rssi = -71 },
    };

    rk_provisioning_status_note_scan(&status, aps, 2);
    assert(status.last_scan_count == 2);
    assert(status.last_scan_best_rssi == -48);
    assert(strcmp(status.last_scan_best_ssid, "Office") == 0);

    rk_provisioning_status_note_scan(&status, NULL, 0);
    assert(status.last_scan_count == 0);
    assert(status.last_scan_best_rssi == 0);
    assert(strcmp(status.last_scan_best_ssid, "") == 0);
}

static void test_format_heartbeat_with_scan_result(void)
{
    rk_provisioning_status_t status = {
        .connected_clients = 2,
        .last_scan_count = 4,
        .last_scan_best_rssi = -55,
        .last_scan_best_ssid = "Office",
    };
    char buffer[160] = {0};
    bool ok = rk_provisioning_format_heartbeat(&status, buffer, sizeof(buffer));

    assert(ok);
    assert(strcmp(buffer,
        "heartbeat clients=2 last_scan_count=4 strongest=Office rssi=-55") == 0);
}

static void test_format_heartbeat_without_scan_result(void)
{
    rk_provisioning_status_t status = {
        .connected_clients = 0,
        .last_scan_count = 0,
        .last_scan_best_rssi = 0,
        .last_scan_best_ssid = "",
    };
    char buffer[160] = {0};
    bool ok = rk_provisioning_format_heartbeat(&status, buffer, sizeof(buffer));

    assert(ok);
    assert(strcmp(buffer,
        "heartbeat clients=0 last_scan_count=0 strongest=none") == 0);
}

int main(void)
{
    test_note_client_event_saturates_disconnect();
    test_note_scan_updates_summary();
    test_format_heartbeat_with_scan_result();
    test_format_heartbeat_without_scan_result();
    return 0;
}

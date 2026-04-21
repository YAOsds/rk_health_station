#include "system_diag.h"

#include <string.h>

static system_diag_snapshot_t s_snapshot;

static void set_last_error(const char *error)
{
    if (error == NULL) {
        s_snapshot.last_error[0] = '\0';
        return;
    }

    strncpy(s_snapshot.last_error, error, sizeof(s_snapshot.last_error) - 1);
    s_snapshot.last_error[sizeof(s_snapshot.last_error) - 1] = '\0';
}

void system_diag_init(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.stage = SYSTEM_DIAG_STAGE_BOOT;
}

void system_diag_set_stage(system_diag_stage_t stage)
{
    s_snapshot.stage = stage;
}

void system_diag_note_wifi_retry(void)
{
    ++s_snapshot.wifi_retries;
}

void system_diag_note_auth_failure(const char *error)
{
    ++s_snapshot.auth_failures;
    set_last_error(error);
}

void system_diag_note_signal_quality(float confidence, bool finger_detected, float motion_level)
{
    s_snapshot.signal_confidence = confidence;
    s_snapshot.finger_detected = finger_detected;
    s_snapshot.motion_level = motion_level;
}

esp_err_t system_diag_get_snapshot(system_diag_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *snapshot = s_snapshot;
    return ESP_OK;
}

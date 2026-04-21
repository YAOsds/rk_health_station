#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYSTEM_DIAG_STAGE_BOOT = 0,
    SYSTEM_DIAG_STAGE_PROVISIONING,
    SYSTEM_DIAG_STAGE_WIFI,
    SYSTEM_DIAG_STAGE_AUTH,
    SYSTEM_DIAG_STAGE_STREAMING,
    SYSTEM_DIAG_STAGE_FAULT,
} system_diag_stage_t;

typedef struct {
    system_diag_stage_t stage;
    int wifi_retries;
    int auth_failures;
    bool finger_detected;
    float signal_confidence;
    float motion_level;
    char last_error[96];
} system_diag_snapshot_t;

void system_diag_init(void);
void system_diag_set_stage(system_diag_stage_t stage);
void system_diag_note_wifi_retry(void);
void system_diag_note_auth_failure(const char *error);
void system_diag_note_signal_quality(float confidence, bool finger_detected, float motion_level);
esp_err_t system_diag_get_snapshot(system_diag_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

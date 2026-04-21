#pragma once

#include <stddef.h>
#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUTH_STATE_UNAUTHENTICATED = 0,
    AUTH_STATE_PENDING,
    AUTH_STATE_AUTHENTICATED,
    AUTH_STATE_REJECTED,
} auth_client_state_t;

esp_err_t auth_client_build_proof(const rk_device_config_t *config, const char *server_nonce,
    const char *client_nonce, int64_t ts, char *proof_hex, size_t proof_hex_len);
esp_err_t auth_client_authenticate(
    const rk_device_config_t *config,
    const char *firmware_version,
    auth_client_state_t *state);

#ifdef __cplusplus
}
#endif

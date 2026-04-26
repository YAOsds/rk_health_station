#include "telemetry_uploader.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "tcp_client.h"

static const char *TAG = "TELEMETRY_UP";
static rk_device_config_t s_config = {0};
static char s_firmware_version[32] = {0};
static bool s_initialized;
static bool s_authenticated;
static uint32_t s_next_seq = 1;

static bool json_escape_string(const char *input, char *output, size_t output_len)
{
    size_t used = 0;

    if (input == NULL || output == NULL || output_len == 0) {
        return false;
    }

    for (const unsigned char *cursor = (const unsigned char *)input; *cursor != '\0'; ++cursor) {
        const char *replacement = NULL;
        char unicode_escape[7];

        if (*cursor == '"') {
            replacement = "\\\"";
        } else if (*cursor == '\\') {
            replacement = "\\\\";
        } else if (*cursor == '\b') {
            replacement = "\\b";
        } else if (*cursor == '\f') {
            replacement = "\\f";
        } else if (*cursor == '\n') {
            replacement = "\\n";
        } else if (*cursor == '\r') {
            replacement = "\\r";
        } else if (*cursor == '\t') {
            replacement = "\\t";
        } else if (*cursor < 0x20U) {
            snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", (unsigned)*cursor);
            replacement = unicode_escape;
        }

        if (replacement != NULL) {
            const size_t len = strlen(replacement);
            if (used + len >= output_len) {
                return false;
            }
            memcpy(output + used, replacement, len);
            used += len;
        } else {
            if (used + 1 >= output_len) {
                return false;
            }
            output[used++] = (char)*cursor;
        }
    }

    output[used] = '\0';
    return true;
}

esp_err_t telemetry_uploader_init(const rk_device_config_t *config, const char *firmware_version)
{
    if (config == NULL || config->server_ip[0] == '\0' || config->device_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_config, 0, sizeof(s_config));
    memcpy(&s_config, config, sizeof(s_config));
    memset(s_firmware_version, 0, sizeof(s_firmware_version));
    if (firmware_version != NULL) {
        strncpy(s_firmware_version, firmware_version, sizeof(s_firmware_version) - 1);
    }

    tcp_client_disconnect();
    if (tcp_client_configure(s_config.server_ip, (uint16_t)s_config.server_port) != ESP_OK) {
        return ESP_FAIL;
    }

    s_initialized = true;
    s_authenticated = false;
    s_next_seq = 1;
    return ESP_OK;
}

void telemetry_uploader_deinit(void)
{
    s_initialized = false;
    s_authenticated = false;
    s_next_seq = 1;
    memset(&s_config, 0, sizeof(s_config));
    memset(s_firmware_version, 0, sizeof(s_firmware_version));
    tcp_client_disconnect();
}

esp_err_t telemetry_uploader_build_frame(const char *device_id, const char *device_name,
    const char *firmware_version, uint32_t seq, int64_t ts,
    const telemetry_vitals_t *vitals, char *buffer, size_t buffer_len)
{
    int written;
    char escaped_device_id[384];
    char escaped_device_name[384];
    char escaped_firmware_version[192];

    if (device_id == NULL || device_name == NULL || firmware_version == NULL
        || vitals == NULL || buffer == NULL || buffer_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!json_escape_string(device_id, escaped_device_id, sizeof(escaped_device_id))
        || !json_escape_string(device_name, escaped_device_name, sizeof(escaped_device_name))
        || !json_escape_string(firmware_version, escaped_firmware_version, sizeof(escaped_firmware_version))) {
        return ESP_ERR_INVALID_SIZE;
    }

    written = snprintf(buffer, buffer_len,
        "{\"ver\":1,\"type\":\"telemetry_batch\",\"seq\":%" PRIu32 ",\"ts\":%lld,"
        "\"device_id\":\"%s\",\"payload\":{\"heart_rate\":%d,\"spo2\":%.1f,"
        "\"acceleration\":%.3f,\"finger_detected\":%d,\"imu_fall_valid\":%d,"
        "\"imu_fall_class\":%d,\"imu_nonfall_prob\":%.5f,\"imu_preimpact_prob\":%.5f,"
        "\"imu_fall_prob\":%.5f,\"firmware_version\":\"%s\","
        "\"device_name\":\"%s\"}}",
        seq,
        (long long)ts,
        escaped_device_id,
        vitals->heart_rate,
        (double)vitals->spo2,
        (double)vitals->acceleration,
        vitals->finger_detected ? 1 : 0,
        vitals->imu_fall.valid ? 1 : 0,
        (int)vitals->imu_fall.label,
        (double)vitals->imu_fall.non_fall_prob,
        (double)vitals->imu_fall.pre_impact_prob,
        (double)vitals->imu_fall.fall_prob,
        escaped_firmware_version,
        escaped_device_name);
    if (written <= 0 || (size_t)written >= buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t telemetry_uploader_send_json(const char *json_frame, size_t len, auth_client_state_t *state)
{
    auth_client_state_t local_state = AUTH_STATE_UNAUTHENTICATED;
    esp_err_t ret;

    if (state == NULL) {
        state = &local_state;
    }
    *state = AUTH_STATE_UNAUTHENTICATED;

    if (!s_initialized || json_frame == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (tcp_client_connect() != ESP_OK) {
        return ESP_FAIL;
    }

    if (!s_authenticated) {
        ret = auth_client_authenticate(&s_config, s_firmware_version, state);
        if (ret != ESP_OK || *state != AUTH_STATE_AUTHENTICATED) {
            ESP_LOGW(TAG, "auth not ready state=%d ret=%d", (int)*state, (int)ret);
            tcp_client_disconnect();
            s_authenticated = false;
            return ret == ESP_OK ? ESP_FAIL : ret;
        }
        s_authenticated = true;
    }

    ret = tcp_client_send_frame(json_frame, len);
    if (ret != ESP_OK) {
        s_authenticated = false;
        tcp_client_disconnect();
        *state = AUTH_STATE_UNAUTHENTICATED;
        return ret;
    }

    *state = AUTH_STATE_AUTHENTICATED;
    ++s_next_seq;
    return ESP_OK;
}

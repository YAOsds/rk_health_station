#include "telemetry_uploader.h"

#include <stdbool.h>
#include <string.h>

#include "auth_client.h"
#include "esp_log.h"
#include "tcp_client.h"

static const char *TAG = "TELEMETRY_UP";
static rk_device_config_t s_config = {0};
static char s_firmware_version[32] = {0};
static bool s_initialized = false;
static bool s_authenticated = false;

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
    return ESP_OK;
}

void telemetry_uploader_deinit(void)
{
    s_initialized = false;
    s_authenticated = false;
    memset(&s_config, 0, sizeof(s_config));
    memset(s_firmware_version, 0, sizeof(s_firmware_version));
    tcp_client_disconnect();
}

esp_err_t telemetry_uploader_send_json(const char *json_frame, size_t len, auth_client_result_t *result)
{
    auth_client_result_t local_result = AUTH_RETRY;
    esp_err_t ret;

    if (result == NULL) {
        result = &local_result;
    }
    *result = AUTH_RETRY;

    if (!s_initialized || json_frame == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (tcp_client_connect() != ESP_OK) {
        return ESP_FAIL;
    }

    if (!s_authenticated) {
        ret = auth_client_authenticate(&s_config, s_firmware_version, result);
        if (ret != ESP_OK || *result != AUTH_OK) {
            ESP_LOGW(TAG, "auth not ready, result=%d ret=%d", (int)*result, (int)ret);
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
        *result = AUTH_RETRY;
        return ret;
    }

    *result = AUTH_OK;
    return ESP_OK;
}

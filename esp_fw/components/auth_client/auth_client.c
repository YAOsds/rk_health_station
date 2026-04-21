#include "auth_client.h"

#include "esp_log.h"

static const char *TAG = "AUTH_CLIENT";

esp_err_t auth_client_authenticate(
    const rk_device_config_t *config,
    const char *firmware_version,
    auth_client_result_t *result)
{
    if (config == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)firmware_version;

    *result = AUTH_RETRY;
    ESP_LOGW(TAG, "auth client placeholder active for device_id=%s", config->device_id);
    return ESP_ERR_NOT_SUPPORTED;
}

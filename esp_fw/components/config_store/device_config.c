#include "device_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "DEVICE_CFG";
static const char *NAMESPACE = "device_cfg";

static esp_err_t load_string(nvs_handle_t handle, const char *key, char *buffer, size_t buffer_len)
{
    size_t required = buffer_len;
    esp_err_t ret;

    if (buffer == NULL || buffer_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = '\0';
    ret = nvs_get_str(handle, key, buffer, &required);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    return ret;
}

static esp_err_t save_string(nvs_handle_t handle, const char *key, const char *value)
{
    return nvs_set_str(handle, key, value != NULL ? value : "");
}

void rk_device_config_clear(rk_device_config_t *config)
{
    if (config != NULL) {
        memset(config, 0, sizeof(*config));
    }
}

bool rk_device_config_is_complete(const rk_device_config_t *config)
{
    return config != NULL
        && config->wifi_ssid[0] != '\0'
        && config->wifi_password[0] != '\0'
        && config->server_ip[0] != '\0'
        && config->server_port > 0
        && config->device_id[0] != '\0'
        && config->device_name[0] != '\0'
        && config->device_secret[0] != '\0';
}

esp_err_t rk_device_config_load(rk_device_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret;
    int32_t server_port = 0;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rk_device_config_clear(config);
    ret = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "open for load failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = load_string(handle, "wifi_ssid", config->wifi_ssid, sizeof(config->wifi_ssid));
    if (ret == ESP_OK) {
        ret = load_string(handle, "wifi_pass", config->wifi_password, sizeof(config->wifi_password));
    }
    if (ret == ESP_OK) {
        ret = load_string(handle, "server_ip", config->server_ip, sizeof(config->server_ip));
    }
    if (ret == ESP_OK) {
        ret = load_string(handle, "device_id", config->device_id, sizeof(config->device_id));
    }
    if (ret == ESP_OK) {
        ret = load_string(handle, "device_name", config->device_name, sizeof(config->device_name));
    }
    if (ret == ESP_OK) {
        ret = load_string(handle, "device_sec", config->device_secret, sizeof(config->device_secret));
    }
    if (ret == ESP_OK) {
        ret = nvs_get_i32(handle, "server_port", &server_port);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
    }

    nvs_close(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "load failed: %s", esp_err_to_name(ret));
        return ret;
    }

    config->server_port = (int)server_port;
    ESP_LOGI(TAG, "loaded config for device_id=%s", config->device_id);
    return ESP_OK;
}

esp_err_t rk_device_config_save(const rk_device_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret;

    if (!rk_device_config_is_complete(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = save_string(handle, "wifi_ssid", config->wifi_ssid);
    if (ret == ESP_OK) {
        ret = save_string(handle, "wifi_pass", config->wifi_password);
    }
    if (ret == ESP_OK) {
        ret = save_string(handle, "server_ip", config->server_ip);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_i32(handle, "server_port", config->server_port);
    }
    if (ret == ESP_OK) {
        ret = save_string(handle, "device_id", config->device_id);
    }
    if (ret == ESP_OK) {
        ret = save_string(handle, "device_name", config->device_name);
    }
    if (ret == ESP_OK) {
        ret = save_string(handle, "device_sec", config->device_secret);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "saved config for device_id=%s host=%s:%d",
            config->device_id,
            config->server_ip,
            config->server_port);
    }
    return ret;
}

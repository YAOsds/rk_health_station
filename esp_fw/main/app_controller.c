#include "app_controller.h"

#include "auth_client.h"
#include "board_config.h"
#include "device_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "provisioning_portal.h"
#include "system_diag.h"
#include "tcp_client.h"
#include "telemetry_uploader.h"
#include "wifi_manager.h"

static const char *TAG = "APP_CTRL";
static const char *APP_FIRMWARE_VERSION = "0.1.0-dev";

static esp_err_t init_nvs_flash(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

void app_controller_run(void)
{
    rk_device_config_t config = {0};
    auth_client_state_t auth_state = AUTH_STATE_UNAUTHENTICATED;
    esp_err_t ret;

    board_config_init();
    system_diag_init();
    system_diag_set_stage(SYSTEM_DIAG_STAGE_BOOT);
    ESP_ERROR_CHECK(init_nvs_flash());

    if (rk_device_config_load(&config) != ESP_OK || !rk_device_config_is_complete(&config)) {
        system_diag_set_stage(SYSTEM_DIAG_STAGE_PROVISIONING);
        ESP_LOGW(TAG, "device config missing, entering provisioning portal");
        ESP_ERROR_CHECK(provisioning_portal_start());
        return;
    }

    system_diag_set_stage(SYSTEM_DIAG_STAGE_WIFI);
    if (wifi_manager_start(&config) != ESP_OK || !wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "wifi not ready yet");
        return;
    }

    ESP_ERROR_CHECK(tcp_client_configure(config.server_ip, (uint16_t)config.server_port));
    ESP_LOGI(TAG,
        "wifi connected ip=%s rssi=%d, tcp host configured to %s:%d",
        wifi_manager_get_ip(),
        wifi_manager_get_rssi(),
        config.server_ip,
        config.server_port);

    system_diag_set_stage(SYSTEM_DIAG_STAGE_AUTH);
    ret = auth_client_authenticate(&config, APP_FIRMWARE_VERSION, &auth_state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "auth handshake failed: %s", esp_err_to_name(ret));
        return;
    }
    if (auth_state == AUTH_STATE_PENDING) {
        ESP_LOGW(TAG, "device pending approval on RK3588 host");
        return;
    }
    if (auth_state != AUTH_STATE_AUTHENTICATED) {
        ESP_LOGW(TAG, "device rejected by RK3588 host");
        return;
    }

    ESP_ERROR_CHECK(telemetry_uploader_init(&config, APP_FIRMWARE_VERSION));
    system_diag_set_stage(SYSTEM_DIAG_STAGE_STREAMING);
    ESP_LOGI(TAG, "auth completed; telemetry pipeline primed");
}

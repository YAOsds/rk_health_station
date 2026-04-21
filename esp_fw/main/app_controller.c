#include "app_controller.h"

#include "board_config.h"
#include "device_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "provisioning_portal.h"
#include "system_diag.h"

static const char *TAG = "APP_CTRL";

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

    ESP_LOGI(TAG, "config present for device_id=%s, waiting for next-stage networking", config.device_id);
}

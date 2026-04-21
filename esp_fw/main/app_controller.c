#include "app_controller.h"

#include "board_config.h"
#include "esp_log.h"
#include "system_diag.h"

static const char *TAG = "APP_CTRL";

void app_controller_run(void)
{
    board_config_init();
    system_diag_init();
    system_diag_set_stage(SYSTEM_DIAG_STAGE_BOOT);
    ESP_LOGI(TAG, "esp_fw bootstrap ready");
}

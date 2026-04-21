idf_component_register(
    SRCS "led_status.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config esp_driver_gpio freertos log
)

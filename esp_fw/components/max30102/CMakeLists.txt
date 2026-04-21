idf_component_register(
    SRCS "max30102.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config esp_driver_i2c esp_timer log
)

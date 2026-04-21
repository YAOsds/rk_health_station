idf_component_register(
    SRCS "app_main.c" "app_controller.c"
    INCLUDE_DIRS "."
    REQUIRES board_config system_diag config_store provisioning_portal wifi_manager net_client auth_client telemetry max30102 mpu6050 signal_filter heart_rate_algo led_status nvs_flash log
)

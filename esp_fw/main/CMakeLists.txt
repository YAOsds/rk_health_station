idf_component_register(
    SRCS "app_main.c" "app_controller.c"
    INCLUDE_DIRS "."
    REQUIRES board_config system_diag config_store provisioning_portal nvs_flash log
)

idf_component_register(
    SRCS "auth_client.c"
    INCLUDE_DIRS "."
    REQUIRES config_store net_client mbedtls esp_timer esp_hw_support esp_system log
)

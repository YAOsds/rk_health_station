idf_component_register(
    SRCS "provisioning_portal.c"
    INCLUDE_DIRS "include" "."
    REQUIRES config_store esp_event esp_http_server esp_netif esp_wifi esp_system nvs_flash log
)

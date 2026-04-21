idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store esp_event esp_netif esp_wifi lwip log
)

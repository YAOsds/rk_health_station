idf_component_register(
    SRCS "telemetry_uploader.c"
    INCLUDE_DIRS "."
    REQUIRES auth_client config_store net_client log
)

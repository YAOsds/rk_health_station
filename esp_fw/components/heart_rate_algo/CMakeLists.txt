idf_component_register(
    SRCS "heart_rate_algo.c"
    INCLUDE_DIRS "include"
    REQUIRES signal_filter log
)

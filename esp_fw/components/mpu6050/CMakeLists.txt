idf_component_register(
    SRCS "mpu6050.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config driver log
)

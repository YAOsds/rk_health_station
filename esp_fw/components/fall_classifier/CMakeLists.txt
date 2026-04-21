idf_component_register(
    SRCS "fall_classifier.cpp"
    INCLUDE_DIRS "include"
    REQUIRES log esp-dl
    EMBED_FILES "../../models/imu_fall_waist_3class.espdl"
)

set(cmake_dir "${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp-dl/fbs_loader/cmake")
include("${cmake_dir}/utilities.cmake")

idf_component_register(
    SRCS "fall_classifier.cpp"
    INCLUDE_DIRS "include"
    REQUIRES log esp-dl
)

target_add_aligned_binary_data(${COMPONENT_LIB} "../../models/imu_fall_waist_3class.espdl" BINARY)

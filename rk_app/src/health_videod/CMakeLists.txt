if(NOT RK_QT_CORE_TARGET)
    message(FATAL_ERROR "RK_QT_CORE_TARGET is not set; configure Qt at the top level first.")
endif()

if(RK_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Network)
    set(RK_QT_NETWORK_TARGET Qt6::Network)
else()
    find_package(Qt5 REQUIRED COMPONENTS Network)
    set(RK_QT_NETWORK_TARGET Qt5::Network)
endif()

add_executable(health-videod
    main.cpp
    app/video_daemon_app.cpp
    app/video_daemon_app.h
    analysis/gstreamer_analysis_output_backend.cpp
    analysis/gstreamer_analysis_output_backend.h
    analysis/analysis_output_backend.h
    analysis/shared_memory_frame_ring.cpp
    analysis/shared_memory_frame_ring.h
    core/video_service.cpp
    core/video_service.h
    core/video_storage_service.cpp
    core/video_storage_service.h
    pipeline/gstreamer_video_pipeline_backend.cpp
    pipeline/gstreamer_video_pipeline_backend.h
    pipeline/video_pipeline_backend.h
    ipc/video_gateway.cpp
    ipc/video_gateway.h
)

set_target_properties(health-videod PROPERTIES
    AUTOMOC ON
)

target_include_directories(health-videod PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(health-videod PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

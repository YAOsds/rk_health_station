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
    analysis/analysis_frame_converter.h
    analysis/rga_frame_converter.cpp
    analysis/rga_frame_converter.h
    core/video_service.cpp
    core/video_service.h
    core/video_storage_service.cpp
    core/video_storage_service.h
    debug/video_runtime_log_stats.cpp
    debug/video_runtime_log_stats.h
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


target_compile_definitions(health-videod PRIVATE
    RKAPP_ENABLE_RGA_ANALYSIS_CONVERT=$<IF:$<BOOL:${RKAPP_ENABLE_RGA_ANALYSIS_CONVERT}>,1,0>
    RKAPP_ENABLE_INPROCESS_GSTREAMER=$<IF:$<BOOL:${RKAPP_ENABLE_INPROCESS_GSTREAMER}>,1,0>
)

if(RKAPP_ENABLE_RGA_ANALYSIS_CONVERT)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBRGA REQUIRED IMPORTED_TARGET librga)
    target_link_libraries(health-videod PRIVATE PkgConfig::LIBRGA)
endif()

if(RKAPP_ENABLE_INPROCESS_GSTREAMER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
        gstreamer-allocators-1.0
    )
    target_sources(health-videod PRIVATE
        pipeline/inprocess_gstreamer_pipeline.cpp
        pipeline/inprocess_gstreamer_pipeline.h
    )
    target_link_libraries(health-videod PRIVATE PkgConfig::GSTREAMER)
endif()

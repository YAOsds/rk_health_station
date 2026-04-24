if(NOT RK_QT_CORE_TARGET)
    message(FATAL_ERROR "RK_QT_CORE_TARGET is not set; configure Qt at the top level first.")
endif()

if(RK_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Network Gui)
    set(RK_FALL_NETWORK_TARGET Qt6::Network)
    set(RK_FALL_GUI_TARGET Qt6::Gui)
else()
    find_package(Qt5 REQUIRED COMPONENTS Network Gui)
    set(RK_FALL_NETWORK_TARGET Qt5::Network)
    set(RK_FALL_GUI_TARGET Qt5::Gui)
endif()

option(RKAPP_ENABLE_REAL_RKNN_POSE "Enable real RKNN pose runtime for health-falld" OFF)
option(RKAPP_ENABLE_REAL_RKNN_ACTION "Enable real RKNN action runtime for health-falld" OFF)
option(RKAPP_ENABLE_REAL_STGCN "Enable real ST-GCN runtime for health-falld" OFF)
set(RKNN_MODEL_ZOO_ROOT "" CACHE PATH "Path to rknn_model_zoo root for real RKNN pose build")

add_executable(health-falld
    main.cpp
    action/action_backend_kind.h
    action/action_classifier_factory.cpp
    action/action_classifier_factory.h
    action/rknn_action_model_runner.cpp
    action/rknn_action_model_runner.h
    action/rknn_lstm_action_classifier.cpp
    action/rknn_lstm_action_classifier.h
    action/rule_based_action_classifier.cpp
    action/rule_based_action_classifier.h
    action/stgcn_preprocessor.cpp
    action/stgcn_preprocessor.h
    action/action_classifier.h
    action/sequence_buffer.cpp
    action/target_selector.cpp
    action/target_selector.h
    app/fall_daemon_app.cpp
    app/fall_daemon_app.h
    debug/fall_runtime_log_stats.cpp
    debug/fall_runtime_log_stats.h
    domain/fall_detector_service.cpp
    domain/fall_detector_service.h
    domain/fall_event_policy.cpp
    domain/fall_event_policy.h
    ingest/analysis_stream_client.cpp
    ingest/analysis_stream_client.h
    ingest/shared_memory_frame_reader.cpp
    ingest/shared_memory_frame_reader.h
    ipc/fall_gateway.cpp
    ipc/fall_gateway.h
    pose/nv12_preprocessor.cpp
    pose/nv12_preprocessor.h
    pose/pose_stage_timing_logger.cpp
    pose/pose_stage_timing_logger.h
    pose/rknn_pose_estimator.cpp
    pose/rknn_pose_estimator.h
    pose/pose_estimator.h
    pose/pose_types.h
    tracking/association.cpp
    tracking/association.h
    tracking/byte_track_detection.h
    tracking/byte_track_state.h
    tracking/byte_tracker.cpp
    tracking/byte_tracker.h
    tracking/kalman_filter.cpp
    tracking/kalman_filter.h
    tracking/track_manager.cpp
    tracking/track_manager.h
    tracking/track_icon_registry.cpp
    tracking/track_icon_registry.h
    tracking/track_trace_logger.cpp
    tracking/track_trace_logger.h
    tracking/tracked_person.h
    runtime/runtime_config.cpp
    runtime/runtime_config.h
)

set_target_properties(health-falld PROPERTIES
    AUTOMOC ON
)

target_include_directories(health-falld PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
)

target_link_libraries(health-falld PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_FALL_NETWORK_TARGET}
    ${RK_FALL_GUI_TARGET}
)

if(RKAPP_ENABLE_REAL_RKNN_POSE OR RKAPP_ENABLE_REAL_RKNN_ACTION)
    if(RKNN_MODEL_ZOO_ROOT STREQUAL "")
        message(FATAL_ERROR "RKNN_MODEL_ZOO_ROOT must be set when RKNN real runtime is enabled")
    endif()

    set(RKNN_POSE_CPP_ROOT ${RKNN_MODEL_ZOO_ROOT}/examples/yolov8_pose/cpp)
    set(RKNN_POSE_UTILS_ROOT ${RKNN_MODEL_ZOO_ROOT}/utils)
    set(RKNN_POSE_3RDPARTY_ROOT ${RKNN_MODEL_ZOO_ROOT}/3rdparty)

    if(RKAPP_ENABLE_REAL_RKNN_POSE)
        target_sources(health-falld PRIVATE
            ${RKNN_POSE_CPP_ROOT}/postprocess.cc
        )
    endif()

    target_include_directories(health-falld PRIVATE
        ${RKNN_POSE_UTILS_ROOT}
        ${RKNN_POSE_3RDPARTY_ROOT}/rknpu2/include
    )

    if(RKAPP_ENABLE_REAL_RKNN_POSE)
        target_include_directories(health-falld PRIVATE
            ${RKNN_POSE_CPP_ROOT}
        )
    endif()

    if(RKAPP_ENABLE_REAL_RKNN_POSE)
        target_compile_definitions(health-falld PRIVATE
            RKAPP_ENABLE_REAL_RKNN_POSE=1
        )
    endif()

    if(RKAPP_ENABLE_REAL_RKNN_ACTION)
        target_compile_definitions(health-falld PRIVATE
            RKAPP_ENABLE_REAL_RKNN_ACTION=1
        )
    endif()

    target_link_libraries(health-falld PRIVATE
        ${RKNN_POSE_3RDPARTY_ROOT}/rknpu2/Linux/aarch64/librknnrt.so
        dl
    )
endif()

if(RKAPP_ENABLE_REAL_STGCN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPENCV4 REQUIRED IMPORTED_TARGET opencv4)

    target_compile_definitions(health-falld PRIVATE
        RKAPP_ENABLE_REAL_STGCN=1
    )

    target_link_libraries(health-falld PRIVATE
        PkgConfig::OPENCV4
    )
endif()

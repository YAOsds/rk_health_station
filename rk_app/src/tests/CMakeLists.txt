if(NOT RK_QT_TEST_TARGET)
    message(FATAL_ERROR "RK_QT_TEST_TARGET is not set; configure Qt at the top level first.")
endif()

if(RK_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Network Widgets)
    find_package(Qt6 QUIET COMPONENTS Multimedia MultimediaWidgets)
    set(RK_QT_NETWORK_TARGET Qt6::Network)
    set(RK_QT_WIDGETS_TARGET Qt6::Widgets)
    if(TARGET Qt6::Multimedia AND TARGET Qt6::MultimediaWidgets)
        set(RK_HAS_QT_MULTIMEDIA 1)
        set(RK_QT_MULTIMEDIA_TARGET Qt6::Multimedia)
        set(RK_QT_MULTIMEDIA_WIDGETS_TARGET Qt6::MultimediaWidgets)
    else()
        set(RK_HAS_QT_MULTIMEDIA 0)
    endif()
else()
    find_package(Qt5 REQUIRED COMPONENTS Network Widgets)
    find_package(Qt5 QUIET COMPONENTS Multimedia MultimediaWidgets)
    set(RK_QT_NETWORK_TARGET Qt5::Network)
    set(RK_QT_WIDGETS_TARGET Qt5::Widgets)
    if(TARGET Qt5::Multimedia AND TARGET Qt5::MultimediaWidgets)
        set(RK_HAS_QT_MULTIMEDIA 1)
        set(RK_QT_MULTIMEDIA_TARGET Qt5::Multimedia)
        set(RK_QT_MULTIMEDIA_WIDGETS_TARGET Qt5::MultimediaWidgets)
    else()
        set(RK_HAS_QT_MULTIMEDIA 0)
    endif()
endif()


add_executable(video_protocol_test
    protocol_tests/video_protocol_test.cpp
)

set_target_properties(video_protocol_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(video_protocol_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME video_protocol_test COMMAND video_protocol_test)

add_executable(analysis_stream_protocol_test
    protocol_tests/analysis_stream_protocol_test.cpp
)

set_target_properties(analysis_stream_protocol_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(analysis_stream_protocol_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME analysis_stream_protocol_test COMMAND analysis_stream_protocol_test)

add_executable(analysis_frame_descriptor_protocol_test
    protocol_tests/analysis_frame_descriptor_protocol_test.cpp
)

set_target_properties(analysis_frame_descriptor_protocol_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(analysis_frame_descriptor_protocol_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME analysis_frame_descriptor_protocol_test COMMAND analysis_frame_descriptor_protocol_test)

add_executable(latency_marker_writer_test
    protocol_tests/latency_marker_writer_test.cpp
)

set_target_properties(latency_marker_writer_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(latency_marker_writer_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME latency_marker_writer_test COMMAND latency_marker_writer_test)

add_executable(unix_fd_passing_test
    protocol_tests/unix_fd_passing_test.cpp
)

set_target_properties(unix_fd_passing_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(unix_fd_passing_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME unix_fd_passing_test COMMAND unix_fd_passing_test)

add_executable(fall_protocol_test
    protocol_tests/fall_protocol_test.cpp
)

set_target_properties(fall_protocol_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(fall_protocol_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME fall_protocol_test COMMAND fall_protocol_test)

add_executable(app_runtime_config_test
    shared_tests/app_runtime_config_test.cpp
    ../shared/runtime_config/app_runtime_config.cpp
    ../shared/runtime_config/app_runtime_config_loader.cpp
    ../shared/runtime_config/app_runtime_config_validator.cpp
    ../shared/runtime_config/app_runtime_config_paths.cpp
)

set_target_properties(app_runtime_config_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(app_runtime_config_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(app_runtime_config_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME app_runtime_config_test COMMAND app_runtime_config_test)

add_executable(action_classifier_factory_test
    fall_daemon_tests/action_classifier_factory_test.cpp
    ../health_falld/action/action_classifier_factory.cpp
    ../health_falld/action/rknn_action_model_runner.cpp
    ../health_falld/action/rknn_lstm_action_classifier.cpp
    ../health_falld/action/rule_based_action_classifier.cpp
    ../health_falld/action/stgcn_preprocessor.cpp
    ../health_falld/runtime/runtime_config.cpp
)

set_target_properties(action_classifier_factory_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(action_classifier_factory_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(action_classifier_factory_test PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME action_classifier_factory_test COMMAND action_classifier_factory_test)

add_executable(association_test
    fall_daemon_tests/association_test.cpp
    ../health_falld/tracking/association.cpp
)

set_target_properties(association_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(association_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
)

target_link_libraries(association_test PRIVATE
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME association_test COMMAND association_test)

add_executable(byte_tracker_test
    fall_daemon_tests/byte_tracker_test.cpp
    ../health_falld/tracking/association.cpp
    ../health_falld/tracking/byte_tracker.cpp
    ../health_falld/tracking/kalman_filter.cpp
    ../health_falld/tracking/track_trace_logger.cpp
    ../health_falld/tracking/track_icon_registry.cpp
    ../health_falld/runtime/runtime_config.cpp
)

set_target_properties(byte_tracker_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(byte_tracker_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(byte_tracker_test PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME byte_tracker_test COMMAND byte_tracker_test)

add_executable(track_action_context_test
    fall_daemon_tests/track_action_context_test.cpp
    ../health_falld/domain/fall_event_policy.cpp
)

set_target_properties(track_action_context_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(track_action_context_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(track_action_context_test PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME track_action_context_test COMMAND track_action_context_test)

add_executable(track_trace_logger_test
    fall_daemon_tests/track_trace_logger_test.cpp
    ../health_falld/tracking/track_trace_logger.cpp
)

set_target_properties(track_trace_logger_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(track_trace_logger_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(track_trace_logger_test PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME track_trace_logger_test COMMAND track_trace_logger_test)

add_executable(track_icon_registry_test
    fall_daemon_tests/track_icon_registry_test.cpp
    ../health_falld/tracking/track_icon_registry.cpp
)

set_target_properties(track_icon_registry_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(track_icon_registry_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(track_icon_registry_test PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME track_icon_registry_test COMMAND track_icon_registry_test)

add_executable(protocol_codec_test
    protocol_tests/protocol_codec_test.cpp
)

set_target_properties(protocol_codec_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(protocol_codec_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME protocol_codec_test COMMAND protocol_codec_test)

add_executable(ipc_message_test
    protocol_tests/ipc_message_test.cpp
)

set_target_properties(ipc_message_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(ipc_message_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME ipc_message_test COMMAND ipc_message_test)

add_executable(shared_models_contract_test
    protocol_tests/shared_models_contract_test.cpp
)

set_target_properties(shared_models_contract_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(shared_models_contract_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME shared_models_contract_test COMMAND shared_models_contract_test)

add_executable(host_wifi_status_provider_test
    host_tests/host_wifi_status_provider_test.cpp
    ../healthd/host/host_wifi_status_provider.cpp
)

set_target_properties(host_wifi_status_provider_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(host_wifi_status_provider_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../healthd
)

target_link_libraries(host_wifi_status_provider_test PRIVATE
    ${RK_QT_CORE_TARGET}
    ${RK_QT_NETWORK_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME host_wifi_status_provider_test COMMAND host_wifi_status_provider_test)

add_executable(healthd_tcp_smoke_test
    integration_tests/healthd_tcp_smoke_test.cpp
)

set_target_properties(healthd_tcp_smoke_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(healthd_tcp_smoke_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

target_compile_definitions(healthd_tcp_smoke_test PRIVATE
    HEALTHD_TEST_BINARY_PATH="$<TARGET_FILE:healthd>"
)

add_test(NAME healthd_tcp_smoke_test COMMAND healthd_tcp_smoke_test)

add_executable(device_session_test
    integration_tests/device_session_test.cpp
    ../healthd/network/device_session.cpp
)

set_target_properties(device_session_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(device_session_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../healthd
)

target_link_libraries(device_session_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME device_session_test COMMAND device_session_test)

add_executable(device_storage_test
    storage_tests/device_storage_test.cpp
)

set_target_properties(device_storage_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(device_storage_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME device_storage_test COMMAND device_storage_test)

add_executable(telemetry_service_test
    storage_tests/telemetry_service_test.cpp
    ../healthd/device/device_manager.cpp
    ../healthd/telemetry/aggregation_service.cpp
    ../healthd/telemetry/telemetry_service.cpp
)

set_target_properties(telemetry_service_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(telemetry_service_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../healthd
)

target_link_libraries(telemetry_service_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME telemetry_service_test COMMAND telemetry_service_test)

add_executable(alert_engine_test
    alert_tests/alert_engine_test.cpp
    ../healthd/alerts/alert_engine.cpp
)

set_target_properties(alert_engine_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(alert_engine_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../healthd
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(alert_engine_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME alert_engine_test COMMAND alert_engine_test)

add_executable(auth_manager_test
    device_manager_tests/auth_manager_test.cpp
    ../healthd/device/auth_manager.cpp
)

set_target_properties(auth_manager_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(auth_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../healthd
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(auth_manager_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME auth_manager_test COMMAND auth_manager_test)

add_executable(device_admin_test
    device_manager_tests/device_admin_test.cpp
    ../healthd/device/device_manager.cpp
)

set_target_properties(device_admin_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(device_admin_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../healthd
)

target_link_libraries(device_admin_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME device_admin_test COMMAND device_admin_test)

add_executable(ui_gateway_test
    ipc_tests/ui_gateway_test.cpp
    ../healthd/alerts/alert_engine.cpp
    ../healthd/host/host_wifi_status_provider.cpp
    ../healthd/ipc_server/ui_gateway.cpp
    ../healthd/device/device_manager.cpp
)

set_target_properties(ui_gateway_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(ui_gateway_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../healthd
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(ui_gateway_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME ui_gateway_test COMMAND ui_gateway_test)

add_executable(health_ui_smoke_test
    ui_tests/health_ui_smoke_test.cpp
    ../health_ui/ipc_client/ui_ipc_client.cpp
    ../healthd/alerts/alert_engine.cpp
    ../healthd/ipc_server/ui_gateway.cpp
    ../healthd/device/device_manager.cpp
)

set_target_properties(health_ui_smoke_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(health_ui_smoke_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../healthd
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(health_ui_smoke_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)

add_test(NAME health_ui_smoke_test COMMAND health_ui_smoke_test)

add_executable(health_config_ui_test
    ui_tests/health_config_ui_test.cpp
    ../health_config_ui/app/config_editor_app.cpp
    ../health_config_ui/widgets/config_editor_window.cpp
    ../health_config_ui/widgets/config_section_widget.cpp
)

set_target_properties(health_config_ui_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(health_config_ui_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_config_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(health_config_ui_test PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_WIDGETS_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME health_config_ui_test COMMAND health_config_ui_test)

add_executable(dashboard_page_test
    ui_tests/dashboard_page_test.cpp
    ../health_ui/pages/dashboard_page.cpp
)

set_target_properties(dashboard_page_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(dashboard_page_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_ui
)

target_link_libraries(dashboard_page_test PRIVATE
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)

add_test(NAME dashboard_page_test COMMAND dashboard_page_test)

add_executable(ui_app_dashboard_refresh_test
    ui_tests/ui_app_dashboard_refresh_test.cpp
    ../health_ui/app/ui_app.cpp
    ../health_ui/ipc_client/fall_ipc_client.cpp
    ../health_ui/ipc_client/ui_ipc_client.cpp
    ../health_ui/ipc_client/video_ipc_client.cpp
    ../health_ui/pages/alerts_page.cpp
    ../health_ui/pages/dashboard_page.cpp
    ../health_ui/pages/device_approval_page.cpp
    ../health_ui/pages/device_detail_page.cpp
    ../health_ui/pages/device_list_page.cpp
    ../health_ui/pages/history_page.cpp
    ../health_ui/pages/settings_page.cpp
    ../health_ui/pages/video_monitor_page.cpp
    ../health_ui/widgets/video_preview_consumer.cpp
    ../health_ui/widgets/video_preview_widget.cpp
)

set_target_properties(ui_app_dashboard_refresh_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(ui_app_dashboard_refresh_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(ui_app_dashboard_refresh_test PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_NETWORK_TARGET}
    ${RK_QT_TEST_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)

target_compile_definitions(ui_app_dashboard_refresh_test PRIVATE RK_HAS_QT_MULTIMEDIA=${RK_HAS_QT_MULTIMEDIA})

if(RK_HAS_QT_MULTIMEDIA)
    target_link_libraries(ui_app_dashboard_refresh_test PRIVATE
        ${RK_QT_MULTIMEDIA_TARGET}
        ${RK_QT_MULTIMEDIA_WIDGETS_TARGET}
    )
endif()

add_test(NAME ui_app_dashboard_refresh_test COMMAND ui_app_dashboard_refresh_test)

set(VIDEO_PIPELINE_HELPER_SOURCES
    ../health_videod/pipeline/analysis_frame_publisher.cpp
    ../health_videod/pipeline/dma_buffer_allocator.cpp
    ../health_videod/pipeline/gst_command_builder.cpp
    ../health_videod/pipeline/multipart_jpeg_parser.cpp
    ../health_videod/pipeline/preview_stream_reader.cpp
)

add_executable(video_runtime_config_integration_test
    video_daemon_tests/video_runtime_config_integration_test.cpp
    ../health_videod/analysis/gstreamer_analysis_output_backend.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_videod/analysis/rga_frame_converter.cpp
    ../health_videod/app/video_daemon_app.cpp
    ../health_videod/core/video_service.cpp
    ../health_videod/core/video_storage_service.cpp
    ../health_videod/debug/video_runtime_log_stats.cpp
    ../health_videod/ipc/video_gateway.cpp
    ../health_videod/pipeline/gstreamer_video_pipeline_backend.cpp
    ${VIDEO_PIPELINE_HELPER_SOURCES}
)

set_target_properties(video_runtime_config_integration_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(video_runtime_config_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(video_runtime_config_integration_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME video_runtime_config_integration_test COMMAND video_runtime_config_integration_test)

add_executable(video_service_test
    video_daemon_tests/video_service_test.cpp
    ../health_videod/analysis/gstreamer_analysis_output_backend.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_videod/analysis/rga_frame_converter.cpp
    ../health_videod/core/video_service.cpp
    ../health_videod/core/video_storage_service.cpp
    ../health_videod/debug/video_runtime_log_stats.cpp
    ../health_videod/pipeline/gstreamer_video_pipeline_backend.cpp
    ${VIDEO_PIPELINE_HELPER_SOURCES}
)

set_target_properties(video_service_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(video_service_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(video_service_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME video_service_test COMMAND video_service_test)

add_executable(analysis_output_backend_test
    video_daemon_tests/analysis_output_backend_test.cpp
    ../health_videod/analysis/gstreamer_analysis_output_backend.cpp
)

set_target_properties(analysis_output_backend_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(analysis_output_backend_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(analysis_output_backend_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME analysis_output_backend_test COMMAND analysis_output_backend_test)

add_executable(video_service_analysis_test
    video_daemon_tests/video_service_analysis_test.cpp
    ../health_videod/analysis/gstreamer_analysis_output_backend.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_videod/analysis/rga_frame_converter.cpp
    ../health_videod/core/video_service.cpp
    ../health_videod/core/video_storage_service.cpp
    ../health_videod/debug/video_runtime_log_stats.cpp
    ../health_videod/pipeline/gstreamer_video_pipeline_backend.cpp
    ${VIDEO_PIPELINE_HELPER_SOURCES}
)

set_target_properties(video_service_analysis_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(video_service_analysis_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(video_service_analysis_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME video_service_analysis_test COMMAND video_service_analysis_test)

add_executable(video_runtime_log_stats_test
    video_daemon_tests/video_runtime_log_stats_test.cpp
    ../health_videod/debug/video_runtime_log_stats.cpp
)

set_target_properties(video_runtime_log_stats_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(video_runtime_log_stats_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
)

target_link_libraries(video_runtime_log_stats_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME video_runtime_log_stats_test COMMAND video_runtime_log_stats_test)

add_executable(gstreamer_pipeline_components_test
    video_daemon_tests/gstreamer_pipeline_components_test.cpp
    ${VIDEO_PIPELINE_HELPER_SOURCES}
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_videod/debug/video_runtime_log_stats.cpp
)

set_target_properties(gstreamer_pipeline_components_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(gstreamer_pipeline_components_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(gstreamer_pipeline_components_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME gstreamer_pipeline_components_test COMMAND gstreamer_pipeline_components_test)

add_executable(gstreamer_video_pipeline_backend_test
    video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp
    ../health_videod/pipeline/gstreamer_video_pipeline_backend.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_videod/analysis/rga_frame_converter.cpp
    ../health_videod/debug/video_runtime_log_stats.cpp
    ${VIDEO_PIPELINE_HELPER_SOURCES}
)

set_target_properties(gstreamer_video_pipeline_backend_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(gstreamer_video_pipeline_backend_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(gstreamer_video_pipeline_backend_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

target_compile_definitions(gstreamer_video_pipeline_backend_test PRIVATE
    RKAPP_ENABLE_INPROCESS_GSTREAMER=$<IF:$<BOOL:${RKAPP_ENABLE_INPROCESS_GSTREAMER}>,1,0>
)

if(RKAPP_ENABLE_INPROCESS_GSTREAMER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
        gstreamer-allocators-1.0
    )
    target_sources(gstreamer_video_pipeline_backend_test PRIVATE
        ../health_videod/pipeline/inprocess_gstreamer_pipeline.cpp
        ../health_videod/pipeline/inprocess_gstreamer_pipeline.h
    )
    target_link_libraries(gstreamer_video_pipeline_backend_test PRIVATE PkgConfig::GSTREAMER)
endif()

add_test(NAME gstreamer_video_pipeline_backend_test COMMAND gstreamer_video_pipeline_backend_test)

add_executable(shared_memory_frame_ring_test
    video_daemon_tests/shared_memory_frame_ring_test.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_falld/ingest/shared_memory_frame_reader.cpp
)

set_target_properties(shared_memory_frame_ring_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(shared_memory_frame_ring_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(shared_memory_frame_ring_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME shared_memory_frame_ring_test COMMAND shared_memory_frame_ring_test)

add_executable(video_daemon_shutdown_test
    video_daemon_tests/video_daemon_shutdown_test.cpp
)

set_target_properties(video_daemon_shutdown_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(video_daemon_shutdown_test PRIVATE
    ${RK_QT_TEST_TARGET}
)

target_compile_definitions(video_daemon_shutdown_test PRIVATE
    HEALTH_VIDEOD_BINARY="$<TARGET_FILE:health-videod>"
)

add_dependencies(video_daemon_shutdown_test health-videod)

add_test(NAME video_daemon_shutdown_test COMMAND video_daemon_shutdown_test)

add_executable(video_gateway_test
    ipc_tests/video_gateway_test.cpp
    ../health_videod/analysis/gstreamer_analysis_output_backend.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_videod/analysis/rga_frame_converter.cpp
    ../health_videod/ipc/video_gateway.cpp
    ../health_videod/core/video_service.cpp
    ../health_videod/core/video_storage_service.cpp
    ../health_videod/debug/video_runtime_log_stats.cpp
    ../health_videod/pipeline/gstreamer_video_pipeline_backend.cpp
    ${VIDEO_PIPELINE_HELPER_SOURCES}
)

set_target_properties(video_gateway_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(video_gateway_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(video_gateway_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME video_gateway_test COMMAND video_gateway_test)


add_executable(video_monitor_page_test
    ui_tests/video_monitor_page_test.cpp
    ../health_ui/ipc_client/fall_ipc_client.cpp
    ../health_ui/ipc_client/video_ipc_client.cpp
    ../health_ui/pages/video_monitor_page.cpp
    ../health_ui/widgets/video_preview_consumer.cpp
    ../health_ui/widgets/video_preview_widget.cpp
)

set_target_properties(video_monitor_page_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(video_monitor_page_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(video_monitor_page_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)

add_test(NAME video_monitor_page_test COMMAND video_monitor_page_test)

target_compile_definitions(video_monitor_page_test PRIVATE RK_HAS_QT_MULTIMEDIA=${RK_HAS_QT_MULTIMEDIA})
if(RK_HAS_QT_MULTIMEDIA)
    target_link_libraries(video_monitor_page_test PRIVATE
        ${RK_QT_MULTIMEDIA_TARGET}
        ${RK_QT_MULTIMEDIA_WIDGETS_TARGET}
    )
endif()

add_executable(fall_ipc_client_test
    ui_tests/fall_ipc_client_test.cpp
    ../health_ui/ipc_client/fall_ipc_client.cpp
)

set_target_properties(fall_ipc_client_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(fall_ipc_client_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(fall_ipc_client_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME fall_ipc_client_test COMMAND fall_ipc_client_test)

add_executable(video_preview_widget_test
    ui_tests/video_preview_widget_test.cpp
    ../health_ui/widgets/video_preview_consumer.cpp
    ../health_ui/widgets/video_preview_widget.cpp
)

set_target_properties(video_preview_widget_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(video_preview_widget_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(video_preview_widget_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)

add_test(NAME video_preview_widget_test COMMAND video_preview_widget_test)

target_compile_definitions(video_preview_widget_test PRIVATE RK_HAS_QT_MULTIMEDIA=${RK_HAS_QT_MULTIMEDIA})
if(RK_HAS_QT_MULTIMEDIA)
    target_link_libraries(video_preview_widget_test PRIVATE
        ${RK_QT_MULTIMEDIA_TARGET}
        ${RK_QT_MULTIMEDIA_WIDGETS_TARGET}
    )
endif()

add_executable(video_preview_consumer_test
    ui_tests/video_preview_consumer_test.cpp
    ../health_ui/widgets/video_preview_consumer.cpp
)

set_target_properties(video_preview_consumer_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(video_preview_consumer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_ui
)

target_link_libraries(video_preview_consumer_test PRIVATE
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)

add_test(NAME video_preview_consumer_test COMMAND video_preview_consumer_test)

add_executable(fall_gateway_test
    fall_daemon_tests/fall_gateway_test.cpp
    ../health_falld/ipc/fall_gateway.cpp
    ../health_falld/runtime/runtime_config.cpp
)

set_target_properties(fall_gateway_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(fall_gateway_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(fall_gateway_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME fall_gateway_test COMMAND fall_gateway_test)

add_executable(fall_runtime_log_stats_test
    fall_daemon_tests/fall_runtime_log_stats_test.cpp
    ../health_falld/debug/fall_runtime_log_stats.cpp
)

set_target_properties(fall_runtime_log_stats_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(fall_runtime_log_stats_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
)

target_link_libraries(fall_runtime_log_stats_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME fall_runtime_log_stats_test COMMAND fall_runtime_log_stats_test)

add_executable(sequence_buffer_test
    fall_daemon_tests/sequence_buffer_test.cpp
    ../health_falld/action/sequence_buffer.cpp
)

set_target_properties(sequence_buffer_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(sequence_buffer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
)

target_link_libraries(sequence_buffer_test PRIVATE
    ${RK_QT_TEST_TARGET}
)

add_test(NAME sequence_buffer_test COMMAND sequence_buffer_test)

add_executable(target_selector_test
    fall_daemon_tests/target_selector_test.cpp
    ../health_falld/action/target_selector.cpp
)

set_target_properties(target_selector_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(target_selector_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
)

target_link_libraries(target_selector_test PRIVATE
    ${RK_QT_TEST_TARGET}
)

add_test(NAME target_selector_test COMMAND target_selector_test)

add_executable(track_manager_test
    fall_daemon_tests/track_manager_test.cpp
    ../health_falld/tracking/track_manager.cpp
)

set_target_properties(track_manager_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(track_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(track_manager_test PRIVATE
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME track_manager_test COMMAND track_manager_test)

add_executable(analysis_stream_client_test
    fall_daemon_tests/analysis_stream_client_test.cpp
    ../health_falld/ingest/shared_memory_frame_reader.cpp
    ../health_falld/ingest/dmabuf_frame_reader.cpp
    ../health_falld/ingest/analysis_stream_client.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
)

set_target_properties(analysis_stream_client_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(analysis_stream_client_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(analysis_stream_client_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME analysis_stream_client_test COMMAND analysis_stream_client_test)

add_executable(shared_memory_frame_reader_test
    fall_daemon_tests/shared_memory_frame_reader_test.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_falld/ingest/shared_memory_frame_reader.cpp
)

set_target_properties(shared_memory_frame_reader_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(shared_memory_frame_reader_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(shared_memory_frame_reader_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME shared_memory_frame_reader_test COMMAND shared_memory_frame_reader_test)

add_executable(dmabuf_frame_reader_test
    fall_daemon_tests/dmabuf_frame_reader_test.cpp
    ../health_falld/ingest/dmabuf_frame_reader.cpp
)

set_target_properties(dmabuf_frame_reader_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(dmabuf_frame_reader_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(dmabuf_frame_reader_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME dmabuf_frame_reader_test COMMAND dmabuf_frame_reader_test)

add_executable(fall_runtime_pose_stub_test
    fall_daemon_tests/fall_runtime_pose_stub_test.cpp
    ../health_falld/pose/rknn_pose_estimator.cpp
)

set_target_properties(fall_runtime_pose_stub_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(fall_runtime_pose_stub_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(fall_runtime_pose_stub_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME fall_runtime_pose_stub_test COMMAND fall_runtime_pose_stub_test)

add_executable(nv12_preprocessor_test
    fall_daemon_tests/nv12_preprocessor_test.cpp
    ../health_falld/pose/nv12_preprocessor.cpp
)

set_target_properties(nv12_preprocessor_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(nv12_preprocessor_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(nv12_preprocessor_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)

add_test(NAME nv12_preprocessor_test COMMAND nv12_preprocessor_test)

add_executable(rgb_pose_fast_path_test
    fall_daemon_tests/rgb_pose_fast_path_test.cpp
    ../health_falld/pose/nv12_preprocessor.cpp
)

set_target_properties(rgb_pose_fast_path_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(rgb_pose_fast_path_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(rgb_pose_fast_path_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)

add_test(NAME rgb_pose_fast_path_test COMMAND rgb_pose_fast_path_test)

add_executable(pose_stage_timing_logger_test
    fall_daemon_tests/pose_stage_timing_logger_test.cpp
    ../health_falld/pose/pose_stage_timing_logger.cpp
)

set_target_properties(pose_stage_timing_logger_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(pose_stage_timing_logger_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(pose_stage_timing_logger_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME pose_stage_timing_logger_test COMMAND pose_stage_timing_logger_test)

add_executable(fall_event_policy_test
    fall_daemon_tests/fall_event_policy_test.cpp
    ../health_falld/domain/fall_event_policy.cpp
)

set_target_properties(fall_event_policy_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(fall_event_policy_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(fall_event_policy_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME fall_event_policy_test COMMAND fall_event_policy_test)

add_executable(stgcn_preprocessor_test
    fall_daemon_tests/stgcn_preprocessor_test.cpp
    ../health_falld/action/stgcn_preprocessor.cpp
)

set_target_properties(stgcn_preprocessor_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(stgcn_preprocessor_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
)

target_link_libraries(stgcn_preprocessor_test PRIVATE
    ${RK_QT_TEST_TARGET}
)

add_test(NAME stgcn_preprocessor_test COMMAND stgcn_preprocessor_test)

add_executable(rknn_lstm_tensor_shape_test
    fall_daemon_tests/rknn_lstm_tensor_shape_test.cpp
    ../health_falld/action/stgcn_preprocessor.cpp
)

set_target_properties(rknn_lstm_tensor_shape_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(rknn_lstm_tensor_shape_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
)

target_link_libraries(rknn_lstm_tensor_shape_test PRIVATE
    ${RK_QT_TEST_TARGET}
)

add_test(NAME rknn_lstm_tensor_shape_test COMMAND rknn_lstm_tensor_shape_test)

add_executable(rknn_lstm_action_classifier_test
    fall_daemon_tests/rknn_lstm_action_classifier_test.cpp
    ../health_falld/action/rknn_action_model_runner.cpp
    ../health_falld/action/rknn_lstm_action_classifier.cpp
    ../health_falld/action/stgcn_preprocessor.cpp
)

set_target_properties(rknn_lstm_action_classifier_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(rknn_lstm_action_classifier_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(rknn_lstm_action_classifier_test PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME rknn_lstm_action_classifier_test COMMAND rknn_lstm_action_classifier_test)

add_executable(rule_based_action_classifier_test
    fall_daemon_tests/rule_based_action_classifier_test.cpp
    ../health_falld/action/rule_based_action_classifier.cpp
)

set_target_properties(rule_based_action_classifier_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(rule_based_action_classifier_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
)

target_link_libraries(rule_based_action_classifier_test PRIVATE
    ${RK_QT_TEST_TARGET}
)

add_test(NAME rule_based_action_classifier_test COMMAND rule_based_action_classifier_test)

add_executable(fall_detector_service_test
    fall_daemon_tests/fall_detector_service_test.cpp
    ../health_falld/domain/fall_detector_service.cpp
    ../health_falld/domain/fall_event_policy.cpp
)

set_target_properties(fall_detector_service_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(fall_detector_service_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(fall_detector_service_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME fall_detector_service_test COMMAND fall_detector_service_test)

add_executable(fall_end_to_end_status_test
    fall_daemon_tests/fall_end_to_end_status_test.cpp
    ../health_falld/action/action_classifier_factory.cpp
    ../health_falld/action/rknn_action_model_runner.cpp
    ../health_falld/action/rknn_lstm_action_classifier.cpp
    ../health_falld/action/rule_based_action_classifier.cpp
    ../health_falld/action/stgcn_preprocessor.cpp
    ../health_falld/action/target_selector.cpp
    ../health_falld/app/fall_daemon_app.cpp
    ../health_falld/debug/fall_runtime_log_stats.cpp
    ../health_falld/domain/fall_detector_service.cpp
    ../health_falld/domain/fall_event_policy.cpp
    ../health_falld/ingest/analysis_stream_client.cpp
    ../health_falld/ingest/shared_memory_frame_reader.cpp
    ../health_falld/ingest/dmabuf_frame_reader.cpp
    ../health_falld/ipc/fall_gateway.cpp
    ../health_falld/pose/rknn_pose_estimator.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_falld/tracking/association.cpp
    ../health_falld/tracking/byte_tracker.cpp
    ../health_falld/tracking/kalman_filter.cpp
    ../health_falld/tracking/track_trace_logger.cpp
    ../health_falld/tracking/track_icon_registry.cpp
    ../health_falld/runtime/runtime_config.cpp
)

set_target_properties(fall_end_to_end_status_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(fall_end_to_end_status_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(fall_end_to_end_status_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

add_test(NAME fall_end_to_end_status_test COMMAND fall_end_to_end_status_test)


foreach(RK_VIDEOD_RGA_TEST_TARGET
        video_service_test
        video_service_analysis_test
        gstreamer_video_pipeline_backend_test
        video_gateway_test)
    target_compile_definitions(${RK_VIDEOD_RGA_TEST_TARGET} PRIVATE
        RKAPP_ENABLE_RGA_ANALYSIS_CONVERT=$<IF:$<BOOL:${RKAPP_ENABLE_RGA_ANALYSIS_CONVERT}>,1,0>
    )
endforeach()

if(RKAPP_ENABLE_RGA_ANALYSIS_CONVERT)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBRGA REQUIRED IMPORTED_TARGET librga)
    foreach(RK_VIDEOD_RGA_TEST_TARGET
            video_service_test
            video_service_analysis_test
            gstreamer_video_pipeline_backend_test
            video_gateway_test)
        target_link_libraries(${RK_VIDEOD_RGA_TEST_TARGET} PRIVATE PkgConfig::LIBRGA)
    endforeach()
endif()

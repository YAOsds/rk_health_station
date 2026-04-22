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

add_executable(healthd
    main.cpp
    app/daemon_app.cpp
    app/daemon_app.h
    alerts/alert_engine.cpp
    alerts/alert_engine.h
    device/auth_manager.cpp
    device/auth_manager.h
    device/device_manager.cpp
    device/device_manager.h
    host/host_wifi_status.h
    host/host_wifi_status_provider.cpp
    host/host_wifi_status_provider.h
    ipc_server/ui_gateway.cpp
    ipc_server/ui_gateway.h
    network/tcp_acceptor.cpp
    network/tcp_acceptor.h
    network/device_session.cpp
    network/device_session.h
    telemetry/aggregation_service.cpp
    telemetry/aggregation_service.h
    telemetry/telemetry_service.cpp
    telemetry/telemetry_service.h
)

set_target_properties(healthd PROPERTIES
    AUTOMOC ON
)

target_include_directories(healthd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(healthd PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_NETWORK_TARGET}
)

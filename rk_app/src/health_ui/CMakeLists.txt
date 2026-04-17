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

add_executable(health-ui
    main.cpp
    app/ui_app.cpp
    app/ui_app.h
    ipc_client/ui_ipc_client.cpp
    ipc_client/ui_ipc_client.h
    ipc_client/fall_ipc_client.cpp
    ipc_client/fall_ipc_client.h
    ipc_client/video_ipc_client.cpp
    ipc_client/video_ipc_client.h
    pages/dashboard_page.cpp
    pages/dashboard_page.h
    pages/device_list_page.cpp
    pages/device_list_page.h
    pages/device_detail_page.cpp
    pages/device_detail_page.h
    pages/device_approval_page.cpp
    pages/device_approval_page.h
    pages/settings_page.cpp
    pages/settings_page.h
    pages/alerts_page.cpp
    pages/alerts_page.h
    pages/history_page.cpp
    pages/history_page.h
    pages/video_monitor_page.cpp
    pages/video_monitor_page.h
    widgets/video_preview_consumer.cpp
    widgets/video_preview_consumer.h
    widgets/video_preview_widget.cpp
    widgets/video_preview_widget.h
)

set_target_properties(health-ui PROPERTIES
    AUTOMOC ON
)

target_include_directories(health-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(health-ui PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_NETWORK_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)

if(RK_HAS_QT_MULTIMEDIA)
    target_link_libraries(health-ui PRIVATE
        ${RK_QT_MULTIMEDIA_TARGET}
        ${RK_QT_MULTIMEDIA_WIDGETS_TARGET}
    )
endif()

target_compile_definitions(health-ui PRIVATE RK_HAS_QT_MULTIMEDIA=${RK_HAS_QT_MULTIMEDIA})

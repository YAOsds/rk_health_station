if(RK_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    set(RK_CONFIG_UI_WIDGETS_TARGET Qt6::Widgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(RK_CONFIG_UI_WIDGETS_TARGET Qt5::Widgets)
endif()

add_executable(health-config-ui
    main.cpp
    app/config_editor_app.cpp
    app/config_editor_app.h
    widgets/config_editor_window.cpp
    widgets/config_editor_window.h
    widgets/config_section_widget.cpp
    widgets/config_section_widget.h
)

set_target_properties(health-config-ui PROPERTIES
    AUTOMOC ON
)

target_include_directories(health-config-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(health-config-ui PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_CONFIG_UI_WIDGETS_TARGET}
)

if(NOT RK_QT_CORE_TARGET)
    message(FATAL_ERROR "RK_QT_CORE_TARGET is not set; configure Qt at the top level first.")
endif()

if(RK_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Sql)
    set(RK_QT_SQL_TARGET Qt6::Sql)
else()
    find_package(Qt5 REQUIRED COMPONENTS Sql)
    set(RK_QT_SQL_TARGET Qt5::Sql)
endif()

add_library(rk_shared STATIC
    debug/latency_marker_writer.cpp
    protocol/device_frame.cpp
    protocol/ipc_message.cpp
    protocol/unix_fd_passing.cpp
    protocol/analysis_stream_protocol.cpp
    protocol/analysis_frame_descriptor_protocol.cpp
    protocol/fall_ipc.cpp
    protocol/video_ipc.cpp
    security/hmac_helper.cpp
    storage/database.cpp
)

target_include_directories(rk_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rk_shared PUBLIC
    ${RK_QT_CORE_TARGET}
    ${RK_QT_SQL_TARGET}
)

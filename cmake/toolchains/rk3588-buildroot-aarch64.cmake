set(RK3588_SDK_ROOT "/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot" CACHE PATH
    "Path to the verified RK3588 Buildroot SDK root")

if(NOT EXISTS "${RK3588_SDK_ROOT}/bin/aarch64-buildroot-linux-gnu-gcc")
    message(FATAL_ERROR
        "RK3588 SDK compiler not found under ${RK3588_SDK_ROOT}. "
        "Update RK3588_SDK_ROOT before configuring.")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_SYSROOT
    "${RK3588_SDK_ROOT}/aarch64-buildroot-linux-gnu/sysroot"
    CACHE PATH "Target sysroot")

set(CMAKE_C_COMPILER
    "${RK3588_SDK_ROOT}/bin/aarch64-buildroot-linux-gnu-gcc"
    CACHE FILEPATH "Target C compiler")
set(CMAKE_CXX_COMPILER
    "${RK3588_SDK_ROOT}/bin/aarch64-buildroot-linux-gnu-g++"
    CACHE FILEPATH "Target C++ compiler")

set(CMAKE_PROGRAM_PATH "${RK3588_SDK_ROOT}/bin" CACHE STRING "SDK host tools")
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}" CACHE STRING "Cross-compile root path")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_PREFIX_PATH
    "${CMAKE_SYSROOT}/usr/lib/cmake"
    CACHE STRING "Package search roots inside the target sysroot")
set(Qt5_DIR
    "${CMAKE_SYSROOT}/usr/lib/cmake/Qt5"
    CACHE PATH "Qt5 package config root in the target sysroot")

set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
    "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/out/probes-rk3588"}
CXX=${CXX:-/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/aarch64-buildroot-linux-gnu-g++}
PKG_CONFIG=${PKG_CONFIG:-/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/pkg-config}
SYSROOT=${SYSROOT:-/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot}

mkdir -p "$BUILD_DIR"
"$CXX" --sysroot="$SYSROOT" -std=c++17 -O2 \
  $("$PKG_CONFIG" --cflags gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 gstreamer-allocators-1.0) \
  "$ROOT_DIR/deploy/probes/gst_dmabuf_appsink_probe.cpp" \
  $("$PKG_CONFIG" --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 gstreamer-allocators-1.0) \
  -o "$BUILD_DIR/gst_dmabuf_appsink_probe"
file "$BUILD_DIR/gst_dmabuf_appsink_probe"
echo "$BUILD_DIR/gst_dmabuf_appsink_probe"

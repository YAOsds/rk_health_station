#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/out/probes-rk3588"}
CXX=${CXX:-/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/aarch64-buildroot-linux-gnu-g++}
SYSROOT=${SYSROOT:-/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot}

mkdir -p "$BUILD_DIR"
"$CXX" --sysroot="$SYSROOT" -std=c++17 -O2 \
  -I"$SYSROOT/usr/include" \
  "$ROOT_DIR/deploy/probes/rga_dmabuf_probe.cpp" \
  -L"$SYSROOT/usr/lib" -L"$SYSROOT/lib" -lrga \
  -o "$BUILD_DIR/rga_dmabuf_probe"
"$CXX" --sysroot="$SYSROOT" -std=c++17 -O2 \
  -I"$SYSROOT/usr/include" \
  "$ROOT_DIR/deploy/probes/rknn_dmabuf_input_probe.cpp" \
  -L"$SYSROOT/usr/lib" -L"$SYSROOT/lib" -lrknnrt \
  -o "$BUILD_DIR/rknn_dmabuf_input_probe"
file "$BUILD_DIR/rga_dmabuf_probe" "$BUILD_DIR/rknn_dmabuf_input_probe"
echo "$BUILD_DIR"

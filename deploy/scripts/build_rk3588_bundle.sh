#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
SDK_ROOT=${RK3588_SDK_ROOT:-/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot}
TOOLCHAIN_FILE=${TOOLCHAIN_FILE:-${PROJECT_ROOT}/cmake/toolchains/rk3588-buildroot-aarch64.cmake}
BUILD_DIR=${BUILD_DIR:-/tmp/rk_health_station-build-rk3588}
BUNDLE_DIR=${BUNDLE_DIR:-${PROJECT_ROOT}/out/rk3588_bundle}
BUILD_TESTING=${BUILD_TESTING:-OFF}
JOBS=${JOBS:-$(nproc)}
RKNN_MODEL_ZOO_ROOT=${RKNN_MODEL_ZOO_ROOT:-/home/elf/workspace/rknn_model_zoo-2.1.0}
SYSROOT=${SDK_ROOT}/aarch64-buildroot-linux-gnu/sysroot
PLUGIN_SRC_DIR=${SYSROOT}/usr/lib/qt/plugins

verify_path() {
  local path=$1
  if [[ ! -e "${path}" ]]; then
    echo "missing required path: ${path}" >&2
    exit 1
  fi
}

verify_arm64() {
  local binary=$1
  local description
  description=$(file "${binary}")
  echo "${description}"
  if [[ "${description}" != *"ARM aarch64"* ]]; then
    echo "binary is not ARM aarch64: ${binary}" >&2
    exit 1
  fi
}

copy_matches() {
  local source_dir=$1
  shift
  local pattern
  local match

  [[ -d "${source_dir}" ]] || return 0

  shopt -s nullglob
  for pattern in "$@"; do
    for match in "${source_dir}"/${pattern}; do
      cp -a "${match}" "${BUNDLE_DIR}/lib/"
    done
  done
  shopt -u nullglob
}

verify_path "${SDK_ROOT}"
verify_path "${SDK_ROOT}/bin/aarch64-buildroot-linux-gnu-g++"
verify_path "${TOOLCHAIN_FILE}"
verify_path "${PROJECT_ROOT}/rk_app/CMakeLists.txt"
verify_path "${RKNN_MODEL_ZOO_ROOT}/examples/yolov8_pose/model/yolov8n-pose.rknn"
verify_path "${RKNN_MODEL_ZOO_ROOT}/examples/yolov8_pose/model/yolov8_pose_labels_list.txt"
verify_path "${RKNN_MODEL_ZOO_ROOT}/yolo_detect/lstm/exports/lstm_fall.rknn"
verify_path "${RKNN_MODEL_ZOO_ROOT}/yolo_detect/lstm/exports/lstm_fall_weights.json"
verify_path "${RKNN_MODEL_ZOO_ROOT}/3rdparty/rknpu2/Linux/aarch64/librknnrt.so"

cmake -S "${PROJECT_ROOT}/rk_app" \
  -B "${BUILD_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
  -DRK3588_SDK_ROOT="${SDK_ROOT}" \
  -DBUILD_TESTING="${BUILD_TESTING}" \
  -DRKAPP_ENABLE_REAL_RKNN_POSE=ON \
  -DRKAPP_ENABLE_REAL_RKNN_ACTION=ON \
  -DRKNN_MODEL_ZOO_ROOT="${RKNN_MODEL_ZOO_ROOT}" \
  -DRKAPP_ENABLE_RGA_ANALYSIS_CONVERT=ON \
  -DRKAPP_ENABLE_INPROCESS_GSTREAMER=ON

cmake --build "${BUILD_DIR}" --target healthd health-ui health-videod health-falld health-config-ui -j"${JOBS}"

verify_arm64 "${BUILD_DIR}/src/healthd/healthd"
verify_arm64 "${BUILD_DIR}/src/health_ui/health-ui"
verify_arm64 "${BUILD_DIR}/src/health_videod/health-videod"
verify_arm64 "${BUILD_DIR}/src/health_falld/health-falld"
verify_arm64 "${BUILD_DIR}/src/health_config_ui/health-config-ui"

rm -rf "${BUNDLE_DIR}"
mkdir -p \
  "${BUNDLE_DIR}/bin" \
  "${BUNDLE_DIR}/lib" \
  "${BUNDLE_DIR}/lib/app" \
  "${BUNDLE_DIR}/plugins" \
  "${BUNDLE_DIR}/scripts" \
  "${BUNDLE_DIR}/config" \
  "${BUNDLE_DIR}/logs" \
  "${BUNDLE_DIR}/run" \
  "${BUNDLE_DIR}/data" \
  "${BUNDLE_DIR}/assets/models" \
  "${BUNDLE_DIR}/model"

install -m 755 "${BUILD_DIR}/src/healthd/healthd" "${BUNDLE_DIR}/bin/healthd"
install -m 755 "${BUILD_DIR}/src/health_ui/health-ui" "${BUNDLE_DIR}/bin/health-ui"
install -m 755 "${BUILD_DIR}/src/health_videod/health-videod" "${BUNDLE_DIR}/bin/health-videod"
install -m 755 "${BUILD_DIR}/src/health_falld/health-falld" "${BUNDLE_DIR}/bin/health-falld"
install -m 755 "${BUILD_DIR}/src/health_config_ui/health-config-ui" "${BUNDLE_DIR}/bin/health-config-ui"
install -m 644 "${PROJECT_ROOT}/deploy/config/runtime_config.json" "${BUNDLE_DIR}/config/runtime_config.json"
install -m 755 "${PROJECT_ROOT}/deploy/bundle/config.sh" "${BUNDLE_DIR}/scripts/config.sh"
install -m 755 "${PROJECT_ROOT}/deploy/bundle/start.sh" "${BUNDLE_DIR}/scripts/start.sh"
install -m 755 "${PROJECT_ROOT}/deploy/bundle/start_all.sh" "${BUNDLE_DIR}/scripts/start_all.sh"
install -m 755 "${PROJECT_ROOT}/deploy/bundle/stop.sh" "${BUNDLE_DIR}/scripts/stop.sh"
install -m 755 "${PROJECT_ROOT}/deploy/bundle/stop_all.sh" "${BUNDLE_DIR}/scripts/stop_all.sh"
install -m 755 "${PROJECT_ROOT}/deploy/bundle/status.sh" "${BUNDLE_DIR}/scripts/status.sh"
install -m 644 "${PROJECT_ROOT}/deploy/bundle/README.txt" "${BUNDLE_DIR}/README.txt"

copy_matches "${SYSROOT}/usr/lib" \
  'libQt5*.so*' \
  'libicu*.so*' \
  'libdouble-conversion.so*' \
  'libpcre2*.so*' \
  'libsqlite3.so*' \
  'libpng*.so*' \
  'libjpeg*.so*' \
  'libfreetype.so*' \
  'libharfbuzz.so*' \
  'libbrotli*.so*' \
  'libglib-2.0.so*' \
  'libgobject-2.0.so*' \
  'libgthread-2.0.so*' \
  'libstdc++.so*' \
  'libgcc_s.so*' \
  'libz.so*'
copy_matches "${SYSROOT}/lib" \
  'libstdc++.so*' \
  'libgcc_s.so*'

if [[ -d "${PLUGIN_SRC_DIR}" ]]; then
  cp -a "${PLUGIN_SRC_DIR}/." "${BUNDLE_DIR}/plugins/"
fi

install -m 755 "${RKNN_MODEL_ZOO_ROOT}/3rdparty/rknpu2/Linux/aarch64/librknnrt.so" \
  "${BUNDLE_DIR}/lib/app/librknnrt.so"
install -m 644 "${RKNN_MODEL_ZOO_ROOT}/examples/yolov8_pose/model/yolov8n-pose.rknn" \
  "${BUNDLE_DIR}/assets/models/yolov8n-pose.rknn"
install -m 644 "${RKNN_MODEL_ZOO_ROOT}/yolo_detect/lstm/exports/lstm_fall.rknn" \
  "${BUNDLE_DIR}/assets/models/lstm_fall.rknn"
install -m 644 "${RKNN_MODEL_ZOO_ROOT}/yolo_detect/lstm/exports/lstm_fall_weights.json" \
  "${BUNDLE_DIR}/assets/models/lstm_fall_weights.json"
install -m 644 "${RKNN_MODEL_ZOO_ROOT}/examples/yolov8_pose/model/yolov8_pose_labels_list.txt" \
  "${BUNDLE_DIR}/model/yolov8_pose_labels_list.txt"
if [[ -f "${RKNN_MODEL_ZOO_ROOT}/yolo_detect/stgcn/exports/stgcn_fall.onnx" ]]; then
  install -m 644 "${RKNN_MODEL_ZOO_ROOT}/yolo_detect/stgcn/exports/stgcn_fall.onnx" \
    "${BUNDLE_DIR}/assets/models/stgcn_fall.onnx"
fi

cat > "${BUNDLE_DIR}/bundle.env" <<ENVEOF
RK_APP_CONFIG_PATH=\${RK_APP_CONFIG_PATH:-\${PWD}/config/runtime_config.json}
RK_RUNTIME_MODE=\${RK_RUNTIME_MODE:-auto}
ENVEOF

cat <<INFO
Bundle generated:
  ${BUNDLE_DIR}

Key paths:
  binary: ${BUNDLE_DIR}/bin/healthd
  binary: ${BUNDLE_DIR}/bin/health-ui
  binary: ${BUNDLE_DIR}/bin/health-videod
  binary: ${BUNDLE_DIR}/bin/health-falld
  binary: ${BUNDLE_DIR}/bin/health-config-ui
  config: ${BUNDLE_DIR}/config/runtime_config.json
  script: ${BUNDLE_DIR}/scripts/config.sh
  script: ${BUNDLE_DIR}/scripts/start.sh
  script: ${BUNDLE_DIR}/scripts/start_all.sh
  script: ${BUNDLE_DIR}/scripts/stop.sh
  script: ${BUNDLE_DIR}/scripts/stop_all.sh
  script: ${BUNDLE_DIR}/scripts/status.sh

Transfer example:
  rsync -av ${BUNDLE_DIR}/ <rk_user>@<rk_ip>:/home/<rk_user>/rk3588_bundle/
INFO

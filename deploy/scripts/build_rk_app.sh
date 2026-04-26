#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
SOURCE_DIR="${PROJECT_ROOT}/rk_app"
JOBS=${JOBS:-$(nproc)}
BUILD_TESTING=${BUILD_TESTING:-OFF}
DEFAULT_TARGETS="healthd health-ui health-videod health-falld"
TARGETS_WAS_SET=0
if [[ -v TARGETS ]]; then
  TARGETS_WAS_SET=1
fi
TARGETS=${TARGETS:-${DEFAULT_TARGETS}}

usage() {
  cat <<'EOF'
Usage:
  bash deploy/scripts/build_rk_app.sh host [extra-cmake-args...]
  bash deploy/scripts/build_rk_app.sh rk3588 [extra-cmake-args...]

Modes:
  host      Build rk_app for the current Ubuntu host.
  rk3588    Cross-build rk_app for RK3588 using the repo toolchain.

Common env overrides:
  BUILD_DIR
  BUILD_TESTING
  JOBS
  TARGETS

RK3588-only env overrides:
  RK3588_SDK_ROOT
  TOOLCHAIN_FILE
  RKNN_MODEL_ZOO_ROOT
  RKAPP_ENABLE_REAL_RKNN_POSE
  RKAPP_ENABLE_REAL_RKNN_ACTION
  RKAPP_ENABLE_REAL_STGCN
EOF
}

log() {
  printf '[build_rk_app] %s\n' "$*"
}

die() {
  printf '[build_rk_app] %s\n' "$*" >&2
  exit 1
}

verify_path() {
  local path=$1
  if [[ ! -e "${path}" ]]; then
    die "missing required path: ${path}"
  fi
}

print_binary_info() {
  local binary=$1
  if [[ -x "${binary}" ]]; then
    file "${binary}"
  else
    log "binary not found: ${binary}"
  fi
}

MODE=${1:-}
if [[ "${MODE}" == "" || "${MODE}" == "help" || "${MODE}" == "--help" || "${MODE}" == "-h" ]]; then
  usage
  exit 0
fi
shift

verify_path "${SOURCE_DIR}/CMakeLists.txt"

CMAKE_ARGS=("$@")
CONFIGURE_ARGS=()
BUILD_TESTING_EFFECTIVE=${BUILD_TESTING}
for arg in "${CMAKE_ARGS[@]}"; do
  case "${arg}" in
    -DBUILD_TESTING=*)
      BUILD_TESTING_EFFECTIVE=${arg#-DBUILD_TESTING=}
      ;;
  esac
done
read -r -a BUILD_TARGETS <<< "${TARGETS}"

case "${MODE}" in
  host)
    BUILD_DIR=${BUILD_DIR:-${PROJECT_ROOT}/out/build-rk_app-host}
    CONFIGURE_ARGS=(
      -S "${SOURCE_DIR}"
      -B "${BUILD_DIR}"
      -DBUILD_TESTING="${BUILD_TESTING_EFFECTIVE}"
      -DRKAPP_ENABLE_REAL_RKNN_POSE=OFF
      -DRKAPP_ENABLE_REAL_RKNN_ACTION=OFF
    )
    ;;
  rk3588)
    BUILD_DIR=${BUILD_DIR:-${PROJECT_ROOT}/out/build-rk_app-rk3588}
    SDK_ROOT=${RK3588_SDK_ROOT:-/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot}
    TOOLCHAIN_FILE=${TOOLCHAIN_FILE:-${PROJECT_ROOT}/cmake/toolchains/rk3588-buildroot-aarch64.cmake}
    RKAPP_ENABLE_REAL_RKNN_POSE=${RKAPP_ENABLE_REAL_RKNN_POSE:-ON}
    RKAPP_ENABLE_REAL_RKNN_ACTION=${RKAPP_ENABLE_REAL_RKNN_ACTION:-ON}
    RKAPP_ENABLE_REAL_STGCN=${RKAPP_ENABLE_REAL_STGCN:-OFF}
    RKNN_MODEL_ZOO_ROOT=${RKNN_MODEL_ZOO_ROOT:-/home/elf/workspace/rknn_model_zoo-2.1.0}

    verify_path "${SDK_ROOT}"
    verify_path "${SDK_ROOT}/bin/aarch64-buildroot-linux-gnu-g++"
    verify_path "${TOOLCHAIN_FILE}"

    CONFIGURE_ARGS=(
      -S "${SOURCE_DIR}"
      -B "${BUILD_DIR}"
      -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}"
      -DRK3588_SDK_ROOT="${SDK_ROOT}"
      -DBUILD_TESTING="${BUILD_TESTING_EFFECTIVE}"
      -DRKAPP_ENABLE_REAL_RKNN_POSE="${RKAPP_ENABLE_REAL_RKNN_POSE}"
      -DRKAPP_ENABLE_REAL_RKNN_ACTION="${RKAPP_ENABLE_REAL_RKNN_ACTION}"
      -DRKAPP_ENABLE_REAL_STGCN="${RKAPP_ENABLE_REAL_STGCN}"
      -DRKAPP_ENABLE_RGA_ANALYSIS_CONVERT=ON
    )

    if [[ "${RKAPP_ENABLE_REAL_RKNN_POSE}" == "ON" || "${RKAPP_ENABLE_REAL_RKNN_ACTION}" == "ON" ]]; then
      verify_path "${RKNN_MODEL_ZOO_ROOT}"
      CONFIGURE_ARGS+=(-DRKNN_MODEL_ZOO_ROOT="${RKNN_MODEL_ZOO_ROOT}")
    fi
    ;;
  *)
    usage
    die "unsupported build mode: ${MODE}"
    ;;
esac

log "mode: ${MODE}"
log "source dir: ${SOURCE_DIR}"
log "build dir: ${BUILD_DIR}"
if [[ "${MODE}" == "host" && "${BUILD_TESTING_EFFECTIVE}" == "ON" && "${TARGETS_WAS_SET}" == "0" ]]; then
  BUILD_TARGETS=()
  log "targets: all"
else
  log "targets: ${TARGETS}"
fi
log "jobs: ${JOBS}"
log "build testing: ${BUILD_TESTING_EFFECTIVE}"

if [[ "${MODE}" == "rk3588" ]]; then
  log "sdk root: ${SDK_ROOT}"
  log "toolchain file: ${TOOLCHAIN_FILE}"
  log "real rknn pose: ${RKAPP_ENABLE_REAL_RKNN_POSE}"
  log "real rknn action: ${RKAPP_ENABLE_REAL_RKNN_ACTION}"
  log "real stgcn: ${RKAPP_ENABLE_REAL_STGCN}"
  if [[ "${RKAPP_ENABLE_REAL_RKNN_POSE}" == "ON" || "${RKAPP_ENABLE_REAL_RKNN_ACTION}" == "ON" ]]; then
    log "rknn model zoo root: ${RKNN_MODEL_ZOO_ROOT}"
  fi
fi

cmake "${CONFIGURE_ARGS[@]}" "${CMAKE_ARGS[@]}"
if (( ${#BUILD_TARGETS[@]} > 0 )); then
  cmake --build "${BUILD_DIR}" --target "${BUILD_TARGETS[@]}" -j"${JOBS}"
else
  cmake --build "${BUILD_DIR}" -j"${JOBS}"
fi

cat <<EOF
Build completed:
  mode: ${MODE}
  build dir: ${BUILD_DIR}

Artifacts:
  ${BUILD_DIR}/src/healthd/healthd
  ${BUILD_DIR}/src/health_ui/health-ui
  ${BUILD_DIR}/src/health_videod/health-videod
  ${BUILD_DIR}/src/health_falld/health-falld
EOF

print_binary_info "${BUILD_DIR}/src/healthd/healthd"
print_binary_info "${BUILD_DIR}/src/health_ui/health-ui"
print_binary_info "${BUILD_DIR}/src/health_videod/health-videod"
print_binary_info "${BUILD_DIR}/src/health_falld/health-falld"

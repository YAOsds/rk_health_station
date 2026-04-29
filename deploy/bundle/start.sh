#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUNDLE_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
BIN_DIR="${BUNDLE_ROOT}/bin"
LIB_DIR="${BUNDLE_ROOT}/lib"
APP_LIB_DIR="${LIB_DIR}/app"
PLUGIN_DIR="${BUNDLE_ROOT}/plugins"
LOG_DIR="${BUNDLE_ROOT}/logs"
RUN_DIR="${BUNDLE_ROOT}/run"
DATA_DIR="${BUNDLE_ROOT}/data"
HEALTHD_BIN="${BIN_DIR}/healthd"
UI_BIN="${BIN_DIR}/health-ui"
VIDEOD_BIN="${BIN_DIR}/health-videod"
FALLD_BIN="${BIN_DIR}/health-falld"
HEALTHD_PID_FILE="${RUN_DIR}/healthd.pid"
UI_PID_FILE="${RUN_DIR}/health-ui.pid"
VIDEOD_PID_FILE="${RUN_DIR}/health-videod.pid"
FALLD_PID_FILE="${RUN_DIR}/health-falld.pid"
HEALTHD_LOG="${LOG_DIR}/healthd.log"
UI_LOG="${LOG_DIR}/health-ui.log"
VIDEOD_LOG="${LOG_DIR}/health-videod.log"
FALLD_LOG="${LOG_DIR}/health-falld.log"
BACKEND_ONLY=0

abspath_path() {
  local path=$1

  readlink -m -- "${path}"
}

read_runtime_mode_from_config() {
  local config_path=$1
  tr -d '\r\n' < "${config_path}" | sed -n 's/.*"runtime_mode"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p'
}

CONFIG_PATH=$(abspath_path "${RK_APP_CONFIG_PATH:-${BUNDLE_ROOT}/config/runtime_config.json}")
RUNTIME_MODE=${RK_RUNTIME_MODE:-}

if [[ "${1:-}" == "--backend-only" ]]; then
  BACKEND_ONLY=1
fi

mkdir -p "${LOG_DIR}" "${RUN_DIR}" "${DATA_DIR}"

if [[ ! -f "${CONFIG_PATH}" ]]; then
  echo "missing runtime config: ${CONFIG_PATH}" >&2
  exit 1
fi

if [[ -z "${RUNTIME_MODE}" ]]; then
  RUNTIME_MODE=$(read_runtime_mode_from_config "${CONFIG_PATH}")
fi
RUNTIME_MODE=${RUNTIME_MODE:-auto}

export RK_APP_CONFIG_PATH="${CONFIG_PATH}"

detect_system_runtime() {
  [[ -r /etc/os-release ]] || return 1
  . /etc/os-release
  case "${ID:-}" in
    ubuntu|debian)
      ;;
    *)
      return 1
      ;;
  esac

  [[ -e /lib/aarch64-linux-gnu/libQt5Core.so.5 || -e /usr/lib/aarch64-linux-gnu/libQt5Core.so.5 ]]
}

configure_runtime() {
  local selected_mode="${RUNTIME_MODE}"
  if [[ "${selected_mode}" == "auto" ]] && detect_system_runtime; then
    selected_mode="system"
  elif [[ "${selected_mode}" == "auto" ]]; then
    selected_mode="bundle"
  fi

  case "${selected_mode}" in
    bundle)
      if [[ -d "${APP_LIB_DIR}" ]]; then
        export LD_LIBRARY_PATH="${APP_LIB_DIR}:${LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
      else
        export LD_LIBRARY_PATH="${LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
      fi
      export QT_PLUGIN_PATH="${PLUGIN_DIR}"
      if [[ -d "${PLUGIN_DIR}/platforms" ]]; then
        export QT_QPA_PLATFORM_PLUGIN_PATH="${PLUGIN_DIR}/platforms"
      else
        unset QT_QPA_PLATFORM_PLUGIN_PATH
      fi
      ;;
    system)
      if [[ -d "${APP_LIB_DIR}" ]]; then
        export LD_LIBRARY_PATH="${APP_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
      else
        unset LD_LIBRARY_PATH
      fi
      unset QT_PLUGIN_PATH
      unset QT_QPA_PLATFORM_PLUGIN_PATH
      ;;
    *)
      echo "unsupported RK_RUNTIME_MODE: ${RUNTIME_MODE} (expected auto, bundle, or system)" >&2
      exit 1
      ;;
  esac

  echo "runtime mode: ${selected_mode}"
}

configure_runtime

valid_pid() {
  local pid=$1
  [[ "${pid}" =~ ^[0-9]+$ ]] && (( pid > 0 ))
}

pid_started_from_bundle() {
  local pid=$1
  local cwd
  cwd=$(readlink "/proc/${pid}/cwd" 2>/dev/null || true)
  [[ "${cwd}" == "${BUNDLE_ROOT}" ]]
}

is_running() {
  local pid_file=$1
  [[ -f "${pid_file}" ]] || return 1
  local pid
  pid=$(cat "${pid_file}")
  valid_pid "${pid}" && kill -0 "${pid}" 2>/dev/null && pid_started_from_bundle "${pid}"
}

start_process() {
  local name=$1
  local binary=$2
  local pid_file=$3
  local log_file=$4

  if is_running "${pid_file}"; then
    echo "${name} already running with pid $(cat "${pid_file}")"
    return 0
  fi

  rm -f "${pid_file}"
  (
    cd "${BUNDLE_ROOT}"
    nohup "${binary}" >>"${log_file}" 2>&1 &
    echo $! > "${pid_file}"
  )
  echo "started ${name}, pid $(cat "${pid_file}")"
}

if [[ ! -x "${HEALTHD_BIN}" ]]; then
  echo "missing backend binary: ${HEALTHD_BIN}" >&2
  exit 1
fi
if [[ ! -x "${VIDEOD_BIN}" ]]; then
  echo "missing video binary: ${VIDEOD_BIN}" >&2
  exit 1
fi
if (( BACKEND_ONLY == 0 )) && [[ ! -x "${UI_BIN}" ]]; then
  echo "missing ui binary: ${UI_BIN}" >&2
  exit 1
fi

start_process "healthd" "${HEALTHD_BIN}" "${HEALTHD_PID_FILE}" "${HEALTHD_LOG}"
start_process "health-videod" "${VIDEOD_BIN}" "${VIDEOD_PID_FILE}" "${VIDEOD_LOG}"

if [[ -x "${FALLD_BIN}" ]]; then
  start_process "health-falld" "${FALLD_BIN}" "${FALLD_PID_FILE}" "${FALLD_LOG}"
fi

if (( BACKEND_ONLY == 0 )); then
  if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" && -z "${QT_QPA_PLATFORM:-}" ]]; then
    echo "warning: DISPLAY/WAYLAND_DISPLAY/QT_QPA_PLATFORM not set; UI startup depends on board graphics session" >&2
  fi
  start_process "health-ui" "${UI_BIN}" "${UI_PID_FILE}" "${UI_LOG}"
fi

echo "runtime config: ${RK_APP_CONFIG_PATH}"
echo "bundle root: ${BUNDLE_ROOT}"
echo "healthd log: ${HEALTHD_LOG}"
echo "health-videod log: ${VIDEOD_LOG}"
if [[ -x "${FALLD_BIN}" ]]; then
  echo "health-falld log: ${FALLD_LOG}"
fi
if (( BACKEND_ONLY == 0 )); then
  echo "health-ui log: ${UI_LOG}"
fi

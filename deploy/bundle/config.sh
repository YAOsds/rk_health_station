#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUNDLE_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
BIN_DIR="${BUNDLE_ROOT}/bin"
LIB_DIR="${BUNDLE_ROOT}/lib"
APP_LIB_DIR="${LIB_DIR}/app"
PLUGIN_DIR="${BUNDLE_ROOT}/plugins"
CONFIG_BIN="${BIN_DIR}/health-config-ui"

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

if [[ ! -f "${CONFIG_PATH}" ]]; then
  echo "missing runtime config: ${CONFIG_PATH}" >&2
  exit 1
fi
if [[ ! -x "${CONFIG_BIN}" ]]; then
  echo "missing config ui binary: ${CONFIG_BIN}" >&2
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
}

configure_runtime

cd "${BUNDLE_ROOT}"
exec "${CONFIG_BIN}" --config "${RK_APP_CONFIG_PATH}"

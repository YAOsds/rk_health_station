#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUNDLE_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
LOG_DIR="${BUNDLE_ROOT}/logs"
RUN_DIR="${BUNDLE_ROOT}/run"
DATA_DIR="${BUNDLE_ROOT}/data"
SOCKET_PATH=${RK_HEALTH_STATION_SOCKET_NAME:-${RUN_DIR}/rk_health_station.sock}
VIDEO_SOCKET_PATH=${RK_VIDEO_SOCKET_NAME:-${RUN_DIR}/rk_video.sock}
VIDEO_ANALYSIS_SOCKET_PATH=${RK_VIDEO_ANALYSIS_SOCKET_PATH:-${RUN_DIR}/rk_video_analysis.sock}
FALL_SOCKET_PATH=${RK_FALL_SOCKET_NAME:-${RUN_DIR}/rk_fall.sock}
DB_PATH=${HEALTHD_DB_PATH:-${DATA_DIR}/healthd.sqlite}

show_state() {
  local name=$1
  local pid_file=$2
  if [[ -f "${pid_file}" ]]; then
    local pid
    pid=$(cat "${pid_file}")
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      echo "${name}: running (pid ${pid})"
      return
    fi
    echo "${name}: stale pid file (${pid})"
    return
  fi
  echo "${name}: stopped"
}

show_state "healthd" "${RUN_DIR}/healthd.pid"
show_state "health-videod" "${RUN_DIR}/health-videod.pid"
show_state "health-falld" "${RUN_DIR}/health-falld.pid"
show_state "health-ui" "${RUN_DIR}/health-ui.pid"
echo "socket: ${SOCKET_PATH}"
if [[ -S "${SOCKET_PATH}" ]]; then
  echo "socket state: present"
else
  echo "socket state: missing"
fi
echo "database: ${DB_PATH}"
echo "logs: ${LOG_DIR}"

echo "video socket: ${VIDEO_SOCKET_PATH}"
if [[ -S "${VIDEO_SOCKET_PATH}" ]]; then
  echo "video socket state: present"
else
  echo "video socket state: missing"
fi

echo "analysis socket: ${VIDEO_ANALYSIS_SOCKET_PATH}"
if [[ -S "${VIDEO_ANALYSIS_SOCKET_PATH}" ]]; then
  echo "analysis socket state: present"
else
  echo "analysis socket state: missing"
fi

echo "fall socket: ${FALL_SOCKET_PATH}"
if [[ -S "${FALL_SOCKET_PATH}" ]]; then
  echo "fall socket state: present"
else
  echo "fall socket state: missing"
fi

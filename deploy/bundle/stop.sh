#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUNDLE_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
RUN_DIR="${BUNDLE_ROOT}/run"
SOCKET_PATH=${RK_HEALTH_STATION_SOCKET_NAME:-${RUN_DIR}/rk_health_station.sock}
VIDEO_SOCKET_PATH=${RK_VIDEO_SOCKET_NAME:-${RUN_DIR}/rk_video.sock}
VIDEO_ANALYSIS_SOCKET_PATH=${RK_VIDEO_ANALYSIS_SOCKET_PATH:-${RUN_DIR}/rk_video_analysis.sock}
FALL_SOCKET_PATH=${RK_FALL_SOCKET_NAME:-${RUN_DIR}/rk_fall.sock}

stop_pid_file() {
  local name=$1
  local pid_file=$2
  [[ -f "${pid_file}" ]] || return 0

  local pid
  pid=$(cat "${pid_file}")
  if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
    kill "${pid}" 2>/dev/null || true
    for _ in $(seq 1 50); do
      if ! kill -0 "${pid}" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if kill -0 "${pid}" 2>/dev/null; then
      kill -9 "${pid}" 2>/dev/null || true
    fi
    echo "stopped ${name} (${pid})"
  fi
  rm -f "${pid_file}"
}

stop_pid_file "health-ui" "${RUN_DIR}/health-ui.pid"
stop_pid_file "health-falld" "${RUN_DIR}/health-falld.pid"
stop_pid_file "health-videod" "${RUN_DIR}/health-videod.pid"
stop_pid_file "healthd" "${RUN_DIR}/healthd.pid"
rm -f "${SOCKET_PATH}" "${VIDEO_SOCKET_PATH}" "${VIDEO_ANALYSIS_SOCKET_PATH}" "${FALL_SOCKET_PATH}"

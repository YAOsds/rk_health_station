#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUNDLE_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
RUN_DIR="${BUNDLE_ROOT}/run"
LOG_DIR="${BUNDLE_ROOT}/logs"

detect_display_env() {
  if [[ -n "${DISPLAY:-}" ]]; then
    return 0
  fi

  if [[ -z "${XAUTHORITY:-}" && -f "${HOME}/.Xauthority" ]]; then
    export XAUTHORITY="${HOME}/.Xauthority"
  fi

  if [[ -n "${XAUTHORITY:-}" ]] && command -v xauth >/dev/null 2>&1; then
    local detected_display
    detected_display=$(xauth -f "${XAUTHORITY}" list 2>/dev/null \
      | sed -n 's/.*unix:\([0-9][0-9]*\).*/:\1/p' | head -n1)
    if [[ -n "${detected_display}" ]]; then
      export DISPLAY="${detected_display}"
    fi
  fi

  if [[ -z "${DISPLAY:-}" ]] \
    && (pgrep -u "${USER}" Xorg >/dev/null 2>&1 || pgrep -x Xorg >/dev/null 2>&1); then
    export DISPLAY=:0
  fi

  if [[ -n "${DISPLAY:-}" && -z "${QT_QPA_PLATFORM:-}" ]]; then
    export QT_QPA_PLATFORM=xcb
  fi
}

check_pid_file_running() {
  local pid_file=$1
  [[ -f "${pid_file}" ]] || return 1

  local pid
  pid=$(cat "${pid_file}")
  [[ -n "${pid}" ]] || return 1
  kill -0 "${pid}" 2>/dev/null
}

report_failure() {
  echo "start_all.sh: startup verification failed" >&2
  "${SCRIPT_DIR}/status.sh" || true

  if [[ -f "${LOG_DIR}/health-ui.log" ]]; then
    echo "--- health-ui.log ---" >&2
    tail -n 80 "${LOG_DIR}/health-ui.log" >&2 || true
  fi
  if [[ -f "${LOG_DIR}/health-videod.log" ]]; then
    echo "--- health-videod.log ---" >&2
    tail -n 40 "${LOG_DIR}/health-videod.log" >&2 || true
  fi
  if [[ -f "${LOG_DIR}/healthd.log" ]]; then
    echo "--- healthd.log ---" >&2
    tail -n 40 "${LOG_DIR}/healthd.log" >&2 || true
  fi
}

detect_display_env
"${SCRIPT_DIR}/start.sh"
sleep 3

if ! check_pid_file_running "${RUN_DIR}/healthd.pid" \
  || ! check_pid_file_running "${RUN_DIR}/health-videod.pid" \
  || ! check_pid_file_running "${RUN_DIR}/health-ui.pid"; then
  report_failure
  exit 1
fi

echo "start_all.sh: healthd, health-videod, and health-ui are running"
"${SCRIPT_DIR}/status.sh"

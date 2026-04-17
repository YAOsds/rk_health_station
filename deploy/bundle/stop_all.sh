#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUNDLE_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
RUN_DIR="${BUNDLE_ROOT}/run"

check_pid_file_absent() {
  local pid_file=$1
  [[ ! -f "${pid_file}" ]]
}

"${SCRIPT_DIR}/stop.sh"
sleep 2

if ! check_pid_file_absent "${RUN_DIR}/healthd.pid" \
  || ! check_pid_file_absent "${RUN_DIR}/health-videod.pid" \
  || ! check_pid_file_absent "${RUN_DIR}/health-ui.pid"; then
  echo "stop_all.sh: some pid files still exist after stop" >&2
  "${SCRIPT_DIR}/status.sh" || true
  exit 1
fi

echo "stop_all.sh: healthd, health-videod, and health-ui are stopped"
"${SCRIPT_DIR}/status.sh"

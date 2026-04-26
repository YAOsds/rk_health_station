#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
SOURCE_BUNDLE_DIR="${PROJECT_ROOT}/deploy/bundle"
TMP_ROOT=$(mktemp -d)
OUTSIDER_PID=""

cleanup() {
  if [[ -n "${OUTSIDER_PID}" ]]; then
    kill "${OUTSIDER_PID}" >/dev/null 2>&1 || true
  fi
  rm -rf "${TMP_ROOT}"
}
trap cleanup EXIT

mkdir -p "${TMP_ROOT}/scripts" "${TMP_ROOT}/run"
cp "${SOURCE_BUNDLE_DIR}/stop.sh" "${TMP_ROOT}/scripts/stop.sh"
chmod +x "${TMP_ROOT}/scripts/stop.sh"

sleep 30 &
OUTSIDER_PID=$!
echo "${OUTSIDER_PID}" > "${TMP_ROOT}/run/healthd.pid"

"${TMP_ROOT}/scripts/stop.sh" >/dev/null

if ! kill -0 "${OUTSIDER_PID}" 2>/dev/null; then
  echo "stop.sh killed a process that was not started from the bundle" >&2
  exit 1
fi

if [[ -f "${TMP_ROOT}/run/healthd.pid" ]]; then
  echo "stale healthd.pid was not removed" >&2
  exit 1
fi

echo "stop_pid_guard_test: PASS"

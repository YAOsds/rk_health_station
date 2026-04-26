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

mkdir -p "${TMP_ROOT}/scripts" "${TMP_ROOT}/run" "${TMP_ROOT}/logs" "${TMP_ROOT}/data"
cp "${SOURCE_BUNDLE_DIR}/status.sh" "${TMP_ROOT}/scripts/status.sh"
chmod +x "${TMP_ROOT}/scripts/status.sh"

sleep 30 &
OUTSIDER_PID=$!
echo "${OUTSIDER_PID}" > "${TMP_ROOT}/run/healthd.pid"

output=$("${TMP_ROOT}/scripts/status.sh")

if grep -Fq "healthd: running (pid ${OUTSIDER_PID})" <<<"${output}"; then
  echo "status.sh reported an unrelated process as running healthd" >&2
  exit 1
fi

if ! grep -Fq "healthd: stale pid file (${OUTSIDER_PID})" <<<"${output}"; then
  echo "status.sh did not report the unrelated pid as stale" >&2
  echo "${output}" >&2
  exit 1
fi

echo "status_pid_guard_test: PASS"

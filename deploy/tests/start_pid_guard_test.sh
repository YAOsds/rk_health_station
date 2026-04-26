#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
SOURCE_BUNDLE_DIR="${PROJECT_ROOT}/deploy/bundle"
TMP_ROOT=$(mktemp -d)
OUTSIDER_PID=""

cleanup() {
  if [[ -x "${TMP_ROOT}/scripts/stop.sh" ]]; then
    "${TMP_ROOT}/scripts/stop.sh" >/dev/null 2>&1 || true
  fi
  if [[ -n "${OUTSIDER_PID}" ]]; then
    kill "${OUTSIDER_PID}" >/dev/null 2>&1 || true
  fi
  rm -rf "${TMP_ROOT}"
}
trap cleanup EXIT

mkdir -p \
  "${TMP_ROOT}/bin" \
  "${TMP_ROOT}/lib/app" \
  "${TMP_ROOT}/plugins/platforms" \
  "${TMP_ROOT}/scripts" \
  "${TMP_ROOT}/logs" \
  "${TMP_ROOT}/run" \
  "${TMP_ROOT}/data"

cp "${SOURCE_BUNDLE_DIR}/start.sh" "${TMP_ROOT}/scripts/start.sh"
cp "${SOURCE_BUNDLE_DIR}/stop.sh" "${TMP_ROOT}/scripts/stop.sh"
chmod +x "${TMP_ROOT}/scripts/"*.sh

cat > "${TMP_ROOT}/bin/healthd" <<'EOF_HEALTHD'
#!/usr/bin/env bash
set -euo pipefail

touch "${HEALTHD_STARTED_MARKER:?}"
exec python3 - "${RK_HEALTH_STATION_SOCKET_NAME:?}" <<'PY'
import os
import socket
import sys
import time

path = sys.argv[1]
try:
    os.unlink(path)
except FileNotFoundError:
    pass

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.bind(path)
sock.listen(1)
time.sleep(30)
PY
EOF_HEALTHD

cat > "${TMP_ROOT}/bin/health-videod" <<'EOF_VIDEOD'
#!/usr/bin/env bash
set -euo pipefail
exec sleep 30
EOF_VIDEOD

chmod +x "${TMP_ROOT}/bin/healthd" "${TMP_ROOT}/bin/health-videod"

sleep 30 &
OUTSIDER_PID=$!
echo "${OUTSIDER_PID}" > "${TMP_ROOT}/run/healthd.pid"

HEALTHD_STARTED_MARKER="${TMP_ROOT}/healthd.started" \
RK_RUNTIME_MODE=system \
"${TMP_ROOT}/scripts/start.sh" --backend-only >/dev/null

if [[ ! -f "${TMP_ROOT}/healthd.started" ]]; then
  echo "start.sh treated an unrelated pid as a running healthd" >&2
  exit 1
fi

if ! kill -0 "${OUTSIDER_PID}" 2>/dev/null; then
  echo "start.sh or cleanup killed an unrelated process" >&2
  exit 1
fi

if [[ "$(cat "${TMP_ROOT}/run/healthd.pid")" == "${OUTSIDER_PID}" ]]; then
  echo "healthd.pid still points at the unrelated process" >&2
  exit 1
fi

echo "start_pid_guard_test: PASS"

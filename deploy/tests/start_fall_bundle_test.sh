#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
SOURCE_BUNDLE_DIR="${PROJECT_ROOT}/deploy/bundle"
TMP_ROOT=$(mktemp -d)

cleanup() {
  if [[ -d "${TMP_ROOT}" ]]; then
    if [[ -x "${TMP_ROOT}/scripts/stop.sh" ]]; then
      FALLD_ENV_CAPTURE="${TMP_ROOT}/health-falld.env" \
      "${TMP_ROOT}/scripts/stop.sh" >/dev/null 2>&1 || true
    fi
    rm -rf "${TMP_ROOT}"
  fi
}
trap cleanup EXIT

mkdir -p \
  "${TMP_ROOT}/bin" \
  "${TMP_ROOT}/lib/app" \
  "${TMP_ROOT}/plugins/platforms" \
  "${TMP_ROOT}/scripts" \
  "${TMP_ROOT}/logs" \
  "${TMP_ROOT}/run" \
  "${TMP_ROOT}/data" \
  "${TMP_ROOT}/config" \
  "${TMP_ROOT}/assets/models" \
  "${TMP_ROOT}/model"

cp "${SOURCE_BUNDLE_DIR}/start.sh" "${TMP_ROOT}/scripts/start.sh"
cp "${SOURCE_BUNDLE_DIR}/stop.sh" "${TMP_ROOT}/scripts/stop.sh"
cp "${SOURCE_BUNDLE_DIR}/status.sh" "${TMP_ROOT}/scripts/status.sh"
cp "${PROJECT_ROOT}/deploy/config/runtime_config.json" "${TMP_ROOT}/config/runtime_config.json"
chmod +x "${TMP_ROOT}/scripts/"*.sh

cat > "${TMP_ROOT}/bin/healthd" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

exec python3 - "${PWD}/run/rk_health_station.sock" <<'PY'
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
EOF

cat > "${TMP_ROOT}/bin/health-videod" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

sleep "${HEALTH_VIDEOD_SOCKET_DELAY_SEC:-0}"
exec python3 - "${PWD}/run/rk_video_analysis.sock" <<'PY'
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
EOF

cat > "${TMP_ROOT}/bin/health-ui" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
exec sleep 30
EOF

cat > "${TMP_ROOT}/bin/health-falld" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

env | sort > "${FALLD_ENV_CAPTURE:?}"
exec sleep 30
EOF

chmod +x \
  "${TMP_ROOT}/bin/healthd" \
  "${TMP_ROOT}/bin/health-videod" \
  "${TMP_ROOT}/bin/health-ui" \
  "${TMP_ROOT}/bin/health-falld"

wait_for_file() {
  local file=$1
  for _ in $(seq 1 50); do
    if [[ -f "${file}" ]]; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

assert_file_missing() {
  local file=$1
  if [[ -e "${file}" ]]; then
    echo "unexpected file exists: ${file}" >&2
    exit 1
  fi
}

wait_for_socket() {
  local path=$1
  for _ in $(seq 1 50); do
    if [[ -S "${path}" ]]; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

rm -f "${TMP_ROOT}/health-falld.env" "${TMP_ROOT}/run/"*.pid "${TMP_ROOT}/run/"*.sock

(
  FALLD_ENV_CAPTURE="${TMP_ROOT}/health-falld.env" \
  HEALTH_VIDEOD_SOCKET_DELAY_SEC=2 \
  RK_RUNTIME_MODE=system \
  "${TMP_ROOT}/scripts/start.sh" --backend-only >/dev/null
) &
START_PID=$!

sleep 0.5
assert_file_missing "${TMP_ROOT}/health-falld.env"

if ! wait_for_socket "${TMP_ROOT}/run/rk_video_analysis.sock"; then
  echo "analysis socket was not created by health-videod" >&2
  exit 1
fi

wait "${START_PID}"

FALLD_ENV_CAPTURE="${TMP_ROOT}/health-falld.env" \
RK_RUNTIME_MODE=system \
"${TMP_ROOT}/scripts/status.sh" >/dev/null

if ! wait_for_file "${TMP_ROOT}/health-falld.env"; then
  echo "health-falld was not started by start.sh" >&2
  exit 1
fi

grep -Fxq "RK_APP_CONFIG_PATH=${TMP_ROOT}/config/runtime_config.json" "${TMP_ROOT}/health-falld.env" || {
  echo "missing runtime config env in health-falld.env" >&2
  exit 1
}

echo "start_fall_bundle_test: PASS"

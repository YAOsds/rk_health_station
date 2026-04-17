#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
SOURCE_BUNDLE_DIR="${PROJECT_ROOT}/deploy/bundle"
TMP_ROOT=$(mktemp -d)

cleanup() {
  if [[ -d "${TMP_ROOT}" ]]; then
    if [[ -x "${TMP_ROOT}/scripts/stop.sh" ]]; then
      HEALTHD_ENV_CAPTURE="${TMP_ROOT}/healthd.env" \
      UI_ENV_CAPTURE="${TMP_ROOT}/ui.env" \
      "${TMP_ROOT}/scripts/stop.sh" >/dev/null 2>&1 || true
    fi
    rm -rf "${TMP_ROOT}"
  fi
}
trap cleanup EXIT

mkdir -p \
  "${TMP_ROOT}/bin" \
  "${TMP_ROOT}/lib" \
  "${TMP_ROOT}/lib/app" \
  "${TMP_ROOT}/plugins/platforms" \
  "${TMP_ROOT}/scripts" \
  "${TMP_ROOT}/logs" \
  "${TMP_ROOT}/run" \
  "${TMP_ROOT}/data"

cp "${SOURCE_BUNDLE_DIR}/start.sh" "${TMP_ROOT}/scripts/start.sh"
cp "${SOURCE_BUNDLE_DIR}/stop.sh" "${TMP_ROOT}/scripts/stop.sh"
cp "${SOURCE_BUNDLE_DIR}/status.sh" "${TMP_ROOT}/scripts/status.sh"
chmod +x "${TMP_ROOT}/scripts/"*.sh

cat > "${TMP_ROOT}/bin/healthd" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

env | sort > "${HEALTHD_ENV_CAPTURE:?}"
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
EOF

cat > "${TMP_ROOT}/bin/health-ui" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

env | sort > "${UI_ENV_CAPTURE:?}"
exec sleep 30
EOF

cat > "${TMP_ROOT}/bin/health-videod" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

exec sleep 30
EOF

chmod +x "${TMP_ROOT}/bin/healthd" "${TMP_ROOT}/bin/health-ui" "${TMP_ROOT}/bin/health-videod"

assert_contains() {
  local file=$1
  local expected=$2
  if ! grep -Fxq "${expected}" "${file}"; then
    echo "expected line not found in ${file}: ${expected}" >&2
    exit 1
  fi
}

assert_missing_prefix() {
  local file=$1
  local prefix=$2
  if grep -q "^${prefix}" "${file}"; then
    echo "unexpected ${prefix} entry found in ${file}" >&2
    exit 1
  fi
}

wait_for_file() {
  local file=$1
  for _ in $(seq 1 50); do
    if [[ -f "${file}" ]]; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for ${file}" >&2
  exit 1
}

run_case() {
  local mode=$1

  rm -f \
    "${TMP_ROOT}/healthd.env" \
    "${TMP_ROOT}/ui.env" \
    "${TMP_ROOT}/run/"*.pid \
    "${TMP_ROOT}/run/rk_health_station.sock"

  unset LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH || true

  HEALTHD_ENV_CAPTURE="${TMP_ROOT}/healthd.env" \
  UI_ENV_CAPTURE="${TMP_ROOT}/ui.env" \
  RK_RUNTIME_MODE="${mode}" \
  "${TMP_ROOT}/scripts/start.sh" >/dev/null

  wait_for_file "${TMP_ROOT}/healthd.env"
  wait_for_file "${TMP_ROOT}/ui.env"

  assert_contains "${TMP_ROOT}/healthd.env" "HEALTHD_DB_PATH=${TMP_ROOT}/data/healthd.sqlite"
  assert_contains "${TMP_ROOT}/healthd.env" "RK_HEALTH_STATION_SOCKET_NAME=${TMP_ROOT}/run/rk_health_station.sock"

  if [[ "${mode}" == "bundle" ]]; then
    assert_contains "${TMP_ROOT}/healthd.env" "LD_LIBRARY_PATH=${TMP_ROOT}/lib/app:${TMP_ROOT}/lib"
    assert_contains "${TMP_ROOT}/healthd.env" "QT_PLUGIN_PATH=${TMP_ROOT}/plugins"
    assert_contains "${TMP_ROOT}/healthd.env" "QT_QPA_PLATFORM_PLUGIN_PATH=${TMP_ROOT}/plugins/platforms"
  else
    assert_contains "${TMP_ROOT}/healthd.env" "LD_LIBRARY_PATH=${TMP_ROOT}/lib/app"
    assert_missing_prefix "${TMP_ROOT}/healthd.env" "QT_PLUGIN_PATH="
    assert_missing_prefix "${TMP_ROOT}/healthd.env" "QT_QPA_PLATFORM_PLUGIN_PATH="
  fi

  HEALTHD_ENV_CAPTURE="${TMP_ROOT}/healthd.env" \
  UI_ENV_CAPTURE="${TMP_ROOT}/ui.env" \
  "${TMP_ROOT}/scripts/stop.sh" >/dev/null
}

run_case bundle
run_case system

echo "start_runtime_mode_test: PASS"

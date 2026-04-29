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
  "${TMP_ROOT}/data" \
  "${TMP_ROOT}/config"

cp "${SOURCE_BUNDLE_DIR}/start.sh" "${TMP_ROOT}/scripts/start.sh"
cp "${SOURCE_BUNDLE_DIR}/stop.sh" "${TMP_ROOT}/scripts/stop.sh"
cp "${SOURCE_BUNDLE_DIR}/status.sh" "${TMP_ROOT}/scripts/status.sh"
cp "${PROJECT_ROOT}/deploy/config/runtime_config.json" "${TMP_ROOT}/config/runtime_config.json"
chmod +x "${TMP_ROOT}/scripts/"*.sh

cat > "${TMP_ROOT}/bin/healthd" <<'INNER_EOF'
#!/usr/bin/env bash
set -euo pipefail

env | sort > "${HEALTHD_ENV_CAPTURE:?}"
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
INNER_EOF

cat > "${TMP_ROOT}/bin/health-ui" <<'INNER_EOF'
#!/usr/bin/env bash
set -euo pipefail

env | sort > "${UI_ENV_CAPTURE:?}"
exec sleep 30
INNER_EOF

cat > "${TMP_ROOT}/bin/health-videod" <<'INNER_EOF'
#!/usr/bin/env bash
set -euo pipefail

exec sleep 30
INNER_EOF

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

detect_auto_mode() {
  local distro_id=""
  if [[ -r /etc/os-release ]]; then
    distro_id=$(awk -F= '$1 == "ID" { gsub(/"/, "", $2); print $2 }' /etc/os-release)
  fi

  if [[ ( "${distro_id}" == "ubuntu" || "${distro_id}" == "debian" ) &&
        ( -e /lib/aarch64-linux-gnu/libQt5Core.so.5 || -e /usr/lib/aarch64-linux-gnu/libQt5Core.so.5 ) ]]; then
    echo "system"
  else
    echo "bundle"
  fi
}

write_runtime_config() {
  local path=$1
  local runtime_mode=$2
  cat > "${path}" <<INNER_EOF
{
  "system": {
    "runtime_mode": "${runtime_mode}"
  }
}
INNER_EOF
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

  assert_contains "${TMP_ROOT}/healthd.env" "RK_APP_CONFIG_PATH=${TMP_ROOT}/config/runtime_config.json"
  assert_missing_prefix "${TMP_ROOT}/healthd.env" "HEALTHD_DB_PATH="
  assert_missing_prefix "${TMP_ROOT}/healthd.env" "RK_HEALTH_STATION_SOCKET_NAME="
  assert_missing_prefix "${TMP_ROOT}/healthd.env" "RK_VIDEO_SOCKET_NAME="

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

run_config_runtime_mode_case() {
  local auto_mode
  auto_mode=$(detect_auto_mode)
  local forced_mode="system"
  if [[ "${auto_mode}" == "system" ]]; then
    forced_mode="bundle"
  fi

  write_runtime_config "${TMP_ROOT}/config/runtime_config.json" "${forced_mode}"

  rm -f \
    "${TMP_ROOT}/healthd.env" \
    "${TMP_ROOT}/ui.env" \
    "${TMP_ROOT}/run/"*.pid \
    "${TMP_ROOT}/run/rk_health_station.sock"

  unset RK_RUNTIME_MODE LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH || true

  HEALTHD_ENV_CAPTURE="${TMP_ROOT}/healthd.env" \
  UI_ENV_CAPTURE="${TMP_ROOT}/ui.env" \
  "${TMP_ROOT}/scripts/start.sh" >/dev/null

  wait_for_file "${TMP_ROOT}/healthd.env"
  assert_contains "${TMP_ROOT}/healthd.env" "RK_APP_CONFIG_PATH=${TMP_ROOT}/config/runtime_config.json"

  if [[ "${forced_mode}" == "bundle" ]]; then
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

run_relative_config_path_case() {
  local auto_mode
  auto_mode=$(detect_auto_mode)
  local forced_mode="system"
  if [[ "${auto_mode}" == "system" ]]; then
    forced_mode="bundle"
  fi

  local caller_root="${TMP_ROOT}/caller"
  local relative_config_dir="${caller_root}/custom"
  local absolute_config_path="${relative_config_dir}/runtime_config.json"
  mkdir -p "${relative_config_dir}"
  write_runtime_config "${absolute_config_path}" "${forced_mode}"

  rm -f \
    "${TMP_ROOT}/healthd.env" \
    "${TMP_ROOT}/ui.env" \
    "${TMP_ROOT}/run/"*.pid \
    "${TMP_ROOT}/run/rk_health_station.sock"

  unset RK_RUNTIME_MODE LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH || true

  (
    cd "${caller_root}"
    HEALTHD_ENV_CAPTURE="${TMP_ROOT}/healthd.env" \
    UI_ENV_CAPTURE="${TMP_ROOT}/ui.env" \
    RK_APP_CONFIG_PATH="./custom/runtime_config.json" \
    "${TMP_ROOT}/scripts/start.sh" >/dev/null
  )

  wait_for_file "${TMP_ROOT}/healthd.env"
  assert_contains "${TMP_ROOT}/healthd.env" "RK_APP_CONFIG_PATH=${absolute_config_path}"

  if [[ "${forced_mode}" == "bundle" ]]; then
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

run_missing_relative_config_case() {
  local caller_root="${TMP_ROOT}/missing-caller"
  local stderr_file="${TMP_ROOT}/missing-config.stderr"
  mkdir -p "${caller_root}"
  rm -f "${stderr_file}"

  if (
    cd "${caller_root}"
    RK_APP_CONFIG_PATH="./missing/runtime_config.json" \
    "${TMP_ROOT}/scripts/start.sh" >/dev/null 2>"${stderr_file}"
  ); then
    echo "start.sh unexpectedly succeeded with missing relative config" >&2
    exit 1
  fi

  assert_contains "${stderr_file}" "missing runtime config: ${caller_root}/missing/runtime_config.json"
}

run_case bundle
run_case system
write_runtime_config "${TMP_ROOT}/config/runtime_config.json" "auto"
run_config_runtime_mode_case
run_relative_config_path_case
run_missing_relative_config_case

echo "start_runtime_mode_test: PASS"

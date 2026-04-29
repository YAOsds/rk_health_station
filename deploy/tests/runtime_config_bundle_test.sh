#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
SOURCE_BUNDLE_DIR="${PROJECT_ROOT}/deploy/bundle"
TMP_ROOT=$(mktemp -d)

cleanup() {
  rm -rf "${TMP_ROOT}"
}
trap cleanup EXIT

mkdir -p \
  "${TMP_ROOT}/bin" \
  "${TMP_ROOT}/lib" \
  "${TMP_ROOT}/lib/app" \
  "${TMP_ROOT}/plugins/platforms" \
  "${TMP_ROOT}/scripts" \
  "${TMP_ROOT}/config"

cp "${SOURCE_BUNDLE_DIR}/config.sh" "${TMP_ROOT}/scripts/config.sh"
chmod +x "${TMP_ROOT}/scripts/config.sh"

cat > "${TMP_ROOT}/bin/health-config-ui" <<'INNER_EOF'
#!/usr/bin/env bash
set -euo pipefail

env | sort > "${CONFIG_UI_ENV_CAPTURE:?}"
printf '%s\n' "$@" > "${CONFIG_UI_ARGS_CAPTURE:?}"
INNER_EOF

chmod +x "${TMP_ROOT}/bin/health-config-ui"

assert_contains() {
  local file=$1
  local expected=$2
  if ! grep -Fxq -- "${expected}" "${file}"; then
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

run_default_config_case() {
  local auto_mode
  auto_mode=$(detect_auto_mode)
  local forced_mode="system"
  if [[ "${auto_mode}" == "system" ]]; then
    forced_mode="bundle"
  fi

  write_runtime_config "${TMP_ROOT}/config/runtime_config.json" "${forced_mode}"
  rm -f "${TMP_ROOT}/config_ui.env" "${TMP_ROOT}/config_ui.args"
  unset RK_RUNTIME_MODE RK_APP_CONFIG_PATH LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH || true

  CONFIG_UI_ENV_CAPTURE="${TMP_ROOT}/config_ui.env" \
  CONFIG_UI_ARGS_CAPTURE="${TMP_ROOT}/config_ui.args" \
  "${TMP_ROOT}/scripts/config.sh" >/dev/null

  assert_contains "${TMP_ROOT}/config_ui.env" "RK_APP_CONFIG_PATH=${TMP_ROOT}/config/runtime_config.json"
  assert_contains "${TMP_ROOT}/config_ui.args" "--config"
  assert_contains "${TMP_ROOT}/config_ui.args" "${TMP_ROOT}/config/runtime_config.json"

  if [[ "${forced_mode}" == "bundle" ]]; then
    assert_contains "${TMP_ROOT}/config_ui.env" "LD_LIBRARY_PATH=${TMP_ROOT}/lib/app:${TMP_ROOT}/lib"
    assert_contains "${TMP_ROOT}/config_ui.env" "QT_PLUGIN_PATH=${TMP_ROOT}/plugins"
    assert_contains "${TMP_ROOT}/config_ui.env" "QT_QPA_PLATFORM_PLUGIN_PATH=${TMP_ROOT}/plugins/platforms"
  else
    assert_contains "${TMP_ROOT}/config_ui.env" "LD_LIBRARY_PATH=${TMP_ROOT}/lib/app"
    assert_missing_prefix "${TMP_ROOT}/config_ui.env" "QT_PLUGIN_PATH="
    assert_missing_prefix "${TMP_ROOT}/config_ui.env" "QT_QPA_PLATFORM_PLUGIN_PATH="
  fi
}

run_relative_override_case() {
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

  rm -f "${TMP_ROOT}/config_ui.env" "${TMP_ROOT}/config_ui.args"
  unset RK_RUNTIME_MODE LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH || true

  (
    cd "${caller_root}"
    CONFIG_UI_ENV_CAPTURE="${TMP_ROOT}/config_ui.env" \
    CONFIG_UI_ARGS_CAPTURE="${TMP_ROOT}/config_ui.args" \
    RK_APP_CONFIG_PATH="./custom/runtime_config.json" \
    "${TMP_ROOT}/scripts/config.sh" >/dev/null
  )

  assert_contains "${TMP_ROOT}/config_ui.env" "RK_APP_CONFIG_PATH=${absolute_config_path}"
  assert_contains "${TMP_ROOT}/config_ui.args" "--config"
  assert_contains "${TMP_ROOT}/config_ui.args" "${absolute_config_path}"

  if [[ "${forced_mode}" == "bundle" ]]; then
    assert_contains "${TMP_ROOT}/config_ui.env" "LD_LIBRARY_PATH=${TMP_ROOT}/lib/app:${TMP_ROOT}/lib"
    assert_contains "${TMP_ROOT}/config_ui.env" "QT_PLUGIN_PATH=${TMP_ROOT}/plugins"
    assert_contains "${TMP_ROOT}/config_ui.env" "QT_QPA_PLATFORM_PLUGIN_PATH=${TMP_ROOT}/plugins/platforms"
  else
    assert_contains "${TMP_ROOT}/config_ui.env" "LD_LIBRARY_PATH=${TMP_ROOT}/lib/app"
    assert_missing_prefix "${TMP_ROOT}/config_ui.env" "QT_PLUGIN_PATH="
    assert_missing_prefix "${TMP_ROOT}/config_ui.env" "QT_QPA_PLATFORM_PLUGIN_PATH="
  fi
}

run_missing_relative_override_case() {
  local caller_root="${TMP_ROOT}/missing-caller"
  local stderr_file="${TMP_ROOT}/missing-config.stderr"
  mkdir -p "${caller_root}"
  rm -f "${stderr_file}"

  if (
    cd "${caller_root}"
    RK_APP_CONFIG_PATH="./missing/runtime_config.json" \
    "${TMP_ROOT}/scripts/config.sh" >/dev/null 2>"${stderr_file}"
  ); then
    echo "config.sh unexpectedly succeeded with missing relative config" >&2
    exit 1
  fi

  assert_contains "${stderr_file}" "missing runtime config: ${caller_root}/missing/runtime_config.json"
}

run_default_config_case
run_relative_override_case
run_missing_relative_override_case

echo "runtime_config_bundle_test: PASS"

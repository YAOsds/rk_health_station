#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
SOURCE_BUNDLE_DIR="${PROJECT_ROOT}/deploy/bundle"
TMP_ROOT=$(mktemp -d)

cleanup() {
  if [[ -d "${TMP_ROOT}" ]]; then
    if [[ -f "${TMP_ROOT}/run/healthd.pid" ]]; then
      kill "$(cat "${TMP_ROOT}/run/healthd.pid")" >/dev/null 2>&1 || true
    fi
    if [[ -f "${TMP_ROOT}/run/health-videod.pid" ]]; then
      kill "$(cat "${TMP_ROOT}/run/health-videod.pid")" >/dev/null 2>&1 || true
    fi
    if [[ -f "${TMP_ROOT}/run/health-ui.pid" ]]; then
      kill "$(cat "${TMP_ROOT}/run/health-ui.pid")" >/dev/null 2>&1 || true
    fi
    rm -rf "${TMP_ROOT}"
  fi
}
trap cleanup EXIT

mkdir -p \
  "${TMP_ROOT}/scripts" \
  "${TMP_ROOT}/run" \
  "${TMP_ROOT}/logs" \
  "${TMP_ROOT}/home" \
  "${TMP_ROOT}/bin"

cp "${SOURCE_BUNDLE_DIR}/start_all.sh" "${TMP_ROOT}/scripts/start_all.sh"
chmod +x "${TMP_ROOT}/scripts/start_all.sh"

touch "${TMP_ROOT}/home/.Xauthority"

cat > "${TMP_ROOT}/scripts/start.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
env | sort > "${TMP_ROOT}/captured.env"
sleep 30 & echo \$! > "${TMP_ROOT}/run/healthd.pid"
sleep 30 & echo \$! > "${TMP_ROOT}/run/health-videod.pid"
sleep 30 & echo \$! > "${TMP_ROOT}/run/health-ui.pid"
EOF

cat > "${TMP_ROOT}/scripts/status.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
exit 0
EOF

cat > "${TMP_ROOT}/bin/pgrep" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
exit 1
EOF

cat > "${TMP_ROOT}/bin/xauth" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
echo "rkboard/unix:10  MIT-MAGIC-COOKIE-1  deadbeef"
EOF

chmod +x "${TMP_ROOT}/scripts/start.sh" "${TMP_ROOT}/scripts/status.sh" \
  "${TMP_ROOT}/bin/pgrep" "${TMP_ROOT}/bin/xauth"

HOME="${TMP_ROOT}/home" \
PATH="${TMP_ROOT}/bin:${PATH}" \
USER=tester \
DISPLAY= \
XAUTHORITY= \
"${TMP_ROOT}/scripts/start_all.sh" >/dev/null

grep -Fxq "DISPLAY=:10" "${TMP_ROOT}/captured.env"
grep -Fxq "XAUTHORITY=${TMP_ROOT}/home/.Xauthority" "${TMP_ROOT}/captured.env"
grep -Fxq "QT_QPA_PLATFORM=xcb" "${TMP_ROOT}/captured.env"

echo "start_all_display_detection_test: PASS"

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
  "${TMP_ROOT}/assets/models" \
  "${TMP_ROOT}/model"

cp "${SOURCE_BUNDLE_DIR}/start.sh" "${TMP_ROOT}/scripts/start.sh"
cp "${SOURCE_BUNDLE_DIR}/stop.sh" "${TMP_ROOT}/scripts/stop.sh"
cp "${SOURCE_BUNDLE_DIR}/status.sh" "${TMP_ROOT}/scripts/status.sh"
chmod +x "${TMP_ROOT}/scripts/"*.sh

cat > "${TMP_ROOT}/bin/healthd" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

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

cat > "${TMP_ROOT}/bin/health-videod" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

exec python3 - "${RK_VIDEO_ANALYSIS_SOCKET_PATH:?}" <<'PY'
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

FALLD_ENV_CAPTURE="${TMP_ROOT}/health-falld.env" \
RK_RUNTIME_MODE=system \
"${TMP_ROOT}/scripts/start.sh" --backend-only >/dev/null

if ! wait_for_file "${TMP_ROOT}/health-falld.env"; then
  echo "health-falld was not started by start.sh" >&2
  exit 1
fi

grep -Fxq "RK_VIDEO_ANALYSIS_SOCKET_PATH=${TMP_ROOT}/run/rk_video_analysis.sock" "${TMP_ROOT}/health-falld.env" || {
  echo "missing analysis socket env in health-falld.env" >&2
  exit 1
}
grep -Fxq "RK_FALL_SOCKET_NAME=${TMP_ROOT}/run/rk_fall.sock" "${TMP_ROOT}/health-falld.env" || {
  echo "missing fall socket env in health-falld.env" >&2
  exit 1
}
grep -Fxq "RK_FALL_POSE_MODEL_PATH=${TMP_ROOT}/assets/models/yolov8n-pose.rknn" "${TMP_ROOT}/health-falld.env" || {
  echo "missing pose model env in health-falld.env" >&2
  exit 1
}
grep -Fxq "RK_FALL_ACTION_BACKEND=lstm_rknn" "${TMP_ROOT}/health-falld.env" || {
  echo "missing action backend env in health-falld.env" >&2
  exit 1
}
grep -Fxq "RK_FALL_LSTM_MODEL_PATH=${TMP_ROOT}/assets/models/lstm_fall.rknn" "${TMP_ROOT}/health-falld.env" || {
  echo "missing lstm model env in health-falld.env" >&2
  exit 1
}

echo "start_fall_bundle_test: PASS"

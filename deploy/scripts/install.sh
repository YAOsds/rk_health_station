#!/usr/bin/env bash
set -euo pipefail

PREFIX=${PREFIX:-/usr/local}
BIN_DIR="${PREFIX}/bin"
SYSTEMD_DIR=${SYSTEMD_DIR:-/etc/systemd/system}
STATE_DIR=${STATE_DIR:-/var/lib/rk_health_station}
BUILD_DIR=${1:-/tmp/rk_health_station-build}

if [[ ! -x "${BUILD_DIR}/src/healthd/healthd" ]]; then
  echo "missing backend binary: ${BUILD_DIR}/src/healthd/healthd" >&2
  exit 1
fi
if [[ ! -x "${BUILD_DIR}/src/health_ui/health-ui" ]]; then
  echo "missing ui binary: ${BUILD_DIR}/src/health_ui/health-ui" >&2
  exit 1
fi

install -d "${BIN_DIR}" "${SYSTEMD_DIR}" "${STATE_DIR}"
install -m 755 "${BUILD_DIR}/src/healthd/healthd" "${BIN_DIR}/healthd"
install -m 755 "${BUILD_DIR}/src/health_ui/health-ui" "${BIN_DIR}/health-ui"
install -m 644 rk_health_station/deploy/systemd/healthd.service "${SYSTEMD_DIR}/healthd.service"
install -m 644 rk_health_station/deploy/systemd/health-ui.service "${SYSTEMD_DIR}/health-ui.service"

systemctl daemon-reload
systemctl enable healthd.service
systemctl enable health-ui.service

echo "installed to ${BIN_DIR} and ${SYSTEMD_DIR}"
echo "state dir: ${STATE_DIR}"

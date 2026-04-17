#!/usr/bin/env bash
set -euo pipefail

OUT_DIR=${1:-./rk_health_station_logs_$(date +%Y%m%d_%H%M%S)}
mkdir -p "${OUT_DIR}"

journalctl -u healthd.service -n 500 --no-pager > "${OUT_DIR}/healthd.log" || true
journalctl -u health-ui.service -n 500 --no-pager > "${OUT_DIR}/health-ui.log" || true
systemctl status healthd.service --no-pager > "${OUT_DIR}/healthd.status" || true
systemctl status health-ui.service --no-pager > "${OUT_DIR}/health-ui.status" || true
cp -f /var/lib/rk_health_station/healthd.sqlite "${OUT_DIR}/healthd.sqlite" 2>/dev/null || true

echo "logs collected at ${OUT_DIR}"

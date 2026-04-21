#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
LOG_FILE="${2:-/tmp/esp_fw-session.log}"

cd "$(dirname "$0")/.."
. ~/esp/esp-idf/export.sh >/tmp/esp_export.log

echo "[serial_capture] port=$PORT log=$LOG_FILE"
idf.py -p "$PORT" monitor | tee "$LOG_FILE"

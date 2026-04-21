#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

PYTHON_BIN="../.venv-imu/bin/python"
if [ ! -x "$PYTHON_BIN" ]; then
    PYTHON_BIN="python3"
fi

"$PYTHON_BIN" -m pytest ml/imu_fall_model/tests -q

. ~/esp/esp-idf/export.sh >/tmp/esp_export.log
idf.py build

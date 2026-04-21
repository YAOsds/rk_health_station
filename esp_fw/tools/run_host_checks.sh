#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

PYTHON_BIN="../.venv-imu/bin/python"
if [ ! -x "$PYTHON_BIN" ] || ! "$PYTHON_BIN" -m pytest --version >/dev/null 2>&1; then
    PYTHON_BIN="python3"
fi

"$PYTHON_BIN" -m pytest ml/imu_fall_model/tests -q

HOST_TEST_DIR="/tmp/rk_health_station_esp_fw_host_checks"
mkdir -p "$HOST_TEST_DIR"

cc -std=c11 -Wall -Wextra -Werror -Icomponents/provisioning_portal \
    components/provisioning_portal/test/test_provisioning_scan.c \
    components/provisioning_portal/provisioning_scan.c \
    -o "$HOST_TEST_DIR/test_provisioning_scan"
"$HOST_TEST_DIR/test_provisioning_scan"

cc -std=c11 -Wall -Wextra -Werror -Icomponents/provisioning_portal \
    components/provisioning_portal/test/test_provisioning_status.c \
    components/provisioning_portal/provisioning_status.c \
    -o "$HOST_TEST_DIR/test_provisioning_status"
"$HOST_TEST_DIR/test_provisioning_status"

cc -std=c11 -Wall -Wextra -Werror -Icomponents/provisioning_portal \
    components/provisioning_portal/test/test_provisioning_web_assets.c \
    -o "$HOST_TEST_DIR/test_provisioning_web_assets"
"$HOST_TEST_DIR/test_provisioning_web_assets"

. ~/esp/esp-idf/export.sh >/tmp/esp_export.log
idf.py build

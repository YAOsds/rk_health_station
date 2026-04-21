#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

grep -q "idf.py build" README.md
grep -q "AP provisioning" README.md
grep -q "serial_capture.sh" README.md

. ~/esp/esp-idf/export.sh >/tmp/esp_export.log
idf.py build

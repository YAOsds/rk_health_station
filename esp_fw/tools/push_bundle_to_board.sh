#!/usr/bin/env bash
set -euo pipefail

HOST="${1:-192.168.137.179}"
DEST="${2:-/home/elf/rk3588_bundle_esp_fw}"

cd "$(dirname "$0")/.."

ssh "$HOST" "mkdir -p  /docs"
scp README.md "$HOST:$DEST/README.md"
scp docs/esp-firmware-architecture.md "$HOST:$DEST/docs/esp-firmware-architecture.md"
scp docs/esp-provisioning-guide.md "$HOST:$DEST/docs/esp-provisioning-guide.md"
if [[ -f build/rk_health_station_esp_fw.bin ]]; then
    scp build/rk_health_station_esp_fw.bin "$HOST:$DEST/rk_health_station_esp_fw.bin"
fi
if [[ -f build/bootloader/bootloader.bin ]]; then
    scp build/bootloader/bootloader.bin "$HOST:$DEST/bootloader.bin"
fi
if [[ -f build/partition_table/partition-table.bin ]]; then
    scp build/partition_table/partition-table.bin "$HOST:$DEST/partition-table.bin"
fi

echo "bundle copied to $HOST:$DEST"

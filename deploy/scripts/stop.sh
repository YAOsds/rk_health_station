#!/usr/bin/env bash
set -euo pipefail

systemctl stop health-ui.service || true
systemctl stop healthd.service || true
systemctl --no-pager --full status healthd.service health-ui.service || true

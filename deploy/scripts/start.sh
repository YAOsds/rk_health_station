#!/usr/bin/env bash
set -euo pipefail

systemctl start healthd.service
systemctl start health-ui.service
systemctl --no-pager --full status healthd.service health-ui.service

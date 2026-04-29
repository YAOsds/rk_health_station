#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUNDLE_DIR=$(mktemp -d)
trap 'rm -rf "${BUNDLE_DIR}"' EXIT

mkdir -p "${BUNDLE_DIR}/config" "${BUNDLE_DIR}/scripts" "${BUNDLE_DIR}/bin"
cp "${ROOT}/deploy/bundle/start.sh" "${BUNDLE_DIR}/scripts/start.sh"
cp "${ROOT}/deploy/config/runtime_config.json" "${BUNDLE_DIR}/config/runtime_config.json"

grep -q 'RK_APP_CONFIG_PATH' "${BUNDLE_DIR}/scripts/start.sh"
grep -q '"video"' "${BUNDLE_DIR}/config/runtime_config.json"

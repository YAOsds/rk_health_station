#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
TMP_ROOT=$(mktemp -d)
LOG_FILE="${TMP_ROOT}/cmake.log"

cleanup() {
  rm -rf "${TMP_ROOT}"
}
trap cleanup EXIT

mkdir -p "${TMP_ROOT}/bin" "${TMP_ROOT}/build"
cat > "${TMP_ROOT}/bin/cmake" <<EOF_CMAKE
#!/usr/bin/env bash
set -euo pipefail
printf '%s\\n' "\$*" >> "${LOG_FILE}"
EOF_CMAKE
chmod +x "${TMP_ROOT}/bin/cmake"

PATH="${TMP_ROOT}/bin:${PATH}" \
BUILD_DIR="${TMP_ROOT}/build" \
JOBS=3 \
bash "${PROJECT_ROOT}/deploy/scripts/build_rk_app.sh" host -DBUILD_TESTING=ON >/dev/null

build_line=$(sed -n '2p' "${LOG_FILE}")
if grep -Fq -- '--target' <<<"${build_line}"; then
  echo "host -DBUILD_TESTING=ON still builds only selected app targets" >&2
  echo "build line: ${build_line}" >&2
  exit 1
fi

if ! grep -Fq -- '-j3' <<<"${build_line}"; then
  echo "build line did not preserve JOBS setting" >&2
  echo "build line: ${build_line}" >&2
  exit 1
fi

echo "build_rk_app_testing_targets_test: PASS"

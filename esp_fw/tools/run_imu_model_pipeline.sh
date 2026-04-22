#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
EXT_ROOT="/home/elf/workspace/imu_fall_detect"
MODEL_WORKSPACE="$EXT_ROOT/imu_fall_model"
ARTIFACT_DIR="$EXT_ROOT/artifacts"
CHECK_ONLY=0

usage() {
    cat <<'EOF'
Usage:
  bash tools/run_imu_model_pipeline.sh [--check-only]
  bash tools/run_imu_model_pipeline.sh [artifact_dir] [--check-only]

This script does not train or quantize inside the firmware tree.
It verifies or syncs prebuilt model artifacts from the external workspace.
EOF
}

for arg in "$@"; do
    case "$arg" in
        --check-only)
            CHECK_ONLY=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            ARTIFACT_DIR="$arg"
            ;;
    esac
done

required_file="$ARTIFACT_DIR/imu_fall_waist_3class.espdl"

if [ ! -d "$MODEL_WORKSPACE" ]; then
    echo "missing external model workspace: $MODEL_WORKSPACE" >&2
    exit 1
fi

if [ ! -f "$required_file" ]; then
    echo "missing required artifact: $required_file" >&2
    exit 1
fi

if [ "$CHECK_ONLY" -eq 1 ]; then
    echo "artifacts ready in $ARTIFACT_DIR"
    exit 0
fi

mkdir -p "$ROOT_DIR/models"
cp "$required_file" "$ROOT_DIR/models/"

for optional_name in imu_fall_waist_3class.info imu_fall_waist_3class.json; do
    if [ -f "$ARTIFACT_DIR/$optional_name" ]; then
        cp "$ARTIFACT_DIR/$optional_name" "$ROOT_DIR/models/"
    fi
done

echo "synced imu fall artifacts from $ARTIFACT_DIR into $ROOT_DIR/models"

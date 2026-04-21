#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MODEL_DIR="$ROOT_DIR/ml/imu_fall_model"
ARTIFACT_DIR="${1:-$ROOT_DIR/models}"
DATASET_DIR="${2:-/home/elf/workspace/imu_fall_detect/sisfall-enhanced}"
WORK_DIR="${TMPDIR:-/tmp}/imu_fall_pipeline"
PYTHON_BIN="$ROOT_DIR/../.venv-imu/bin/python"

if [ ! -x "$PYTHON_BIN" ]; then
    PYTHON_BIN="python3"
fi

mkdir -p "$WORK_DIR" "$ARTIFACT_DIR"
"$PYTHON_BIN" "$MODEL_DIR/train.py" --dataset "$DATASET_DIR" --output-dir "$WORK_DIR"
"$PYTHON_BIN" "$MODEL_DIR/evaluate.py" --dataset "$DATASET_DIR" --checkpoint "$WORK_DIR/fallnet_waist_3class.pt"
"$PYTHON_BIN" "$MODEL_DIR/export_onnx.py" --checkpoint "$WORK_DIR/fallnet_waist_3class.pt" --output "$WORK_DIR/fallnet_waist_3class.onnx"
"$PYTHON_BIN" "$MODEL_DIR/quantize_espdl.py" --onnx "$WORK_DIR/fallnet_waist_3class.onnx" --output "$ARTIFACT_DIR/imu_fall_waist_3class.espdl" --calibration-dir "$DATASET_DIR"

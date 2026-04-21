# IMU Fall Model Workspace

## Dataset path

```bash
/home/elf/workspace/imu_fall_detect/sisfall-enhanced
```

Expected files:
- `x_train_3`, `x_val_3`, `x_test_3`
- `y_train_3`, `y_val_3`, `y_test_3`
- `weights_3.txt`

## Quick start

```bash
python -m venv .venv
. .venv/bin/activate
pip install -r esp_fw/ml/imu_fall_model/requirements.txt
python esp_fw/ml/imu_fall_model/train.py   --dataset /home/elf/workspace/imu_fall_detect/sisfall-enhanced   --output-dir /tmp/imu_fall_smoke
```

## Export and quantize

```bash
python esp_fw/ml/imu_fall_model/export_onnx.py \
  --checkpoint /tmp/imu_fall_smoke/fallnet_waist_3class.pt \
  --output /tmp/imu_fall_smoke/fallnet_waist_3class.onnx

python esp_fw/ml/imu_fall_model/quantize_espdl.py \
  --onnx /tmp/imu_fall_smoke/fallnet_waist_3class.onnx \
  --output esp_fw/models/imu_fall_waist_3class.espdl \
  --calibration-dir /home/elf/workspace/imu_fall_detect/sisfall-enhanced \
  --max-samples 64 \
  --batch-size 8
```

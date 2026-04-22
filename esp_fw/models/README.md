# Model Artifacts

Generated model artifacts are produced outside this firmware tree.

Canonical offline workspace:
- `/home/elf/workspace/imu_fall_detect/imu_fall_model`

The firmware build currently requires this artifact in `esp_fw/models/`:
- `imu_fall_waist_3class.espdl`

Optional sidecar metadata may also be synced when present:
- `imu_fall_waist_3class.info`
- `imu_fall_waist_3class.json`

Refresh flow:
1. Generate artifacts in `/home/elf/workspace/imu_fall_detect/artifacts`
2. Run `bash esp_fw/tools/run_imu_model_pipeline.sh`
3. Rebuild firmware with `idf.py build`

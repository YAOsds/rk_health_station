# Model Artifacts

Generated model artifact:
- `imu_fall_waist_3class.espdl`

Refresh flow:
1. `bash esp_fw/tools/run_imu_model_pipeline.sh`
2. Confirm the artifact exists under `esp_fw/models/`
3. Rebuild firmware with `idf.py build`

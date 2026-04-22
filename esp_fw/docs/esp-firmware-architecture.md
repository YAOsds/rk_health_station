# ESP Firmware Architecture

## Runtime overview

The ESP firmware is split into small components and a thin `main/` orchestration layer.

### Boot path

1. `app_main.c` enters `app_controller_run()`.
2. `app_controller` initializes board config, diagnostics, NVS, and LED status.
3. If config is missing, the firmware starts the AP provisioning portal and waits for a reboot after save.
4. If config is valid, the firmware starts three FreeRTOS tasks:
   - `sensor_task`
   - `link_task`
   - `telemetry_task`

## Component responsibilities

### Core platform

- `board_config`: hardware pins and timing defaults
- `system_diag`: low-overhead runtime snapshot for stage / retry / signal quality tracking
- `config_store`: NVS-backed persisted runtime config
- `provisioning_portal`: SoftAP + HTTP form entry for Wi-Fi and RK3588 credentials

### Connectivity

- `wifi_manager`: STA connection lifecycle
- `net_client`: TCP framing transport (`4-byte big-endian length + JSON`)
- `auth_client`: auth handshake codec and proof generation
- `telemetry`: telemetry frame construction and authenticated upload wrapper

### Sensors and algorithms

- `max30102`: PPG sensor driver using ESP-IDF 6 I2C master API
- `mpu6050`: accelerometer / gyro driver using ESP-IDF 6 I2C master API
- `signal_filter`: window analysis, finger detection, signal confidence, motion estimate
- `heart_rate_algo`: converts analyzed signal quality into vital-sign outputs
- `imu_window_buffer`: keeps a rolling `256 x 6` IMU tensor with stride-based emission
- `fall_classifier`: wraps the on-device IMU fall classifier interface, logs model-size/alignment/heap diagnostics, and loads the aligned `.espdl` artifact from flash rodata
- `imu_event_state`: smooths consecutive fall windows before telemetry emission

### UI feedback

- `led_status`: maps runtime state to dual-LED patterns and runs the blink task

## Task model

### `sensor_task`

- initializes MAX30102 and MPU6050
- samples real sensor data continuously
- keeps a rolling PPG window and motion history
- keeps a rolling IMU window for the fall classifier
- computes `heart_rate`, `spo2`, `acceleration`, `finger_detected`
- computes `imu_fall_valid`, `imu_fall_class`, and the three class probabilities
- publishes the latest computed vitals into shared runtime state

### `link_task`

- connects to Wi-Fi
- initializes the telemetry uploader once Wi-Fi is ready
- tracks link loss and restarts the connection loop when Wi-Fi drops

### `telemetry_task`

- waits for both valid vitals and a ready link
- builds `telemetry_batch` JSON frames
- lets the uploader handle auth + send
- updates LED state and diagnostics based on `authenticated / pending / rejected / failed`

## Data flow

1. MAX30102 raw PPG + MPU6050 motion samples enter `sensor_task`.
2. `signal_filter` derives signal quality and motion level.
3. `heart_rate_algo` converts quality metrics into heart rate / SpO2 outputs.
4. `imu_window_buffer` and `fall_classifier` derive a three-class IMU fall state.
5. `telemetry_task` packages those outputs with device metadata.
6. `telemetry_uploader` authenticates and pushes the data to RK3588 `healthd`.

## Debugging signals

- serial logs annotate boot, Wi-Fi state, sensor init, telemetry send, and auth outcomes
- serial logs print fall-classifier memory diagnostics before `esp-dl` model load
- serial logs also print `imu_fall class=<n> probs=[p0 p1 p2]` after each emitted IMU window
- `system_diag` keeps retry counters plus latest signal confidence / motion level
- `led_status` gives physical visibility into the current lifecycle state

## Model artifact ownership

- Offline training and export live in `/home/elf/workspace/imu_fall_detect/imu_fall_model`
- Firmware consumes the generated `.espdl` artifact from `esp_fw/models/`
- `tools/run_imu_model_pipeline.sh` now checks or syncs prebuilt artifacts only; it no longer trains in the firmware tree
- `components/fall_classifier/CMakeLists.txt` uses aligned binary embedding so the flash-rodata model address stays 16-byte aligned for `esp-dl`
- PSRAM is enabled in `sdkconfig.defaults` so `esp-dl` can allocate its runtime buffers without the earlier crash path

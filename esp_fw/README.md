# RK Health Station ESP32 Firmware

Standalone ESP-IDF firmware for the ESP32-S3 side of `rk_health_station`.

## What it does

- boots as a self-contained ESP-IDF project under `esp_fw/`
- exposes an AP provisioning portal when runtime config is missing
- connects to Wi-Fi and the RK3588 `healthd` TCP endpoint
- authenticates with `auth_hello -> auth_challenge -> auth_proof -> auth_result`
- samples MAX30102 and MPU6050 on the real hardware path by default
- computes `heart_rate`, `spo2`, `acceleration`, and `finger_detected`
- assembles rolling MPU6050 windows and emits a three-class IMU fall state
- uploads telemetry frames to the RK3588 backend
- drives dual LEDs to show boot / provisioning / Wi-Fi / pending approval / streaming / fault states

## Build

```bash
cd esp_fw
. ~/esp/esp-idf/export.sh
idf.py build
```

## IMU fall classifier

```bash
bash tools/run_imu_model_pipeline.sh
cd esp_fw
. ~/esp/esp-idf/export.sh
idf.py build
```

This refreshes `models/imu_fall_waist_3class.espdl` before rebuilding the firmware.

## Flash

```bash
cd esp_fw
. ~/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash
```

## Monitor

```bash
bash tools/serial_capture.sh /dev/ttyACM0 /tmp/esp_fw-session.log
```

## AP provisioning

If the device has no valid config, it starts AP provisioning automatically:

- SSID format: `RKHealth-XXXX`
- password: `rkhealth01`
- portal URL: `http://192.168.4.1/`

Required fields in the provisioning form:

- Wi-Fi SSID / password
- RK3588 `server_ip`
- RK3588 `server_port`
- `device_id`
- `device_name`
- `device_secret`

After saving the form, the device stores config in NVS and reboots into STA mode.

## Local verification

```bash
bash tools/run_host_checks.sh
```

This runs the Python-side IMU model tests and then a full `idf.py build`.

## Helpful scripts

- `tools/run_host_checks.sh`: README keyword checks + full build
- `tools/serial_capture.sh`: wraps `idf.py monitor` and saves serial logs
- `tools/push_bundle_to_board.sh`: copies firmware artifacts and docs to the RK3588 side

## More docs

- `docs/esp-firmware-architecture.md`
- `docs/esp-imu-fall-validation-report.md`
- `docs/esp-provisioning-guide.md`

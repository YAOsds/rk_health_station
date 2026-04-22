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
idf.py set-target esp32s3
idf.py build
```

## IMU fall classifier

```bash
bash tools/run_imu_model_pipeline.sh
cd esp_fw
. ~/esp/esp-idf/export.sh
idf.py build
```

This syncs the prebuilt model artifact from `/home/elf/workspace/imu_fall_detect/artifacts`
into `models/imu_fall_waist_3class.espdl` before rebuilding the firmware.

Real IMU fall-model inference requires:
1. Generate artifacts in `/home/elf/workspace/imu_fall_detect/imu_fall_model`
2. Sync artifacts into `esp_fw/models/` with `bash tools/run_imu_model_pipeline.sh`
3. Build firmware with PSRAM enabled
4. Flash and verify `FALL_CLS: esp-dl model loaded successfully`

## Flash

```bash
cd esp_fw
. ~/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 erase_flash # clear flash
idf.py -p /dev/ttyACM0 flash
```

## Monitor

```bash
bash tools/serial_capture.sh /dev/ttyACM0 /tmp/esp_fw-session.log
```

You can also flash and open the serial monitor in one step:

```bash
cd esp_fw
. ~/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with `Ctrl + ]`.

## Debugging on board

Recommended bring-up and debug flow:

1. Enter the firmware directory and load ESP-IDF:

```bash
cd esp_fw
. ~/esp/esp-idf/export.sh
```

2. Check the board serial port:

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Use the actual serial device shown above in later commands. Many boards appear as
`/dev/ttyACM0`, while CH340-based boards often appear as `/dev/ttyUSB0`.

3. Rebuild and flash:

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

4. Watch the serial log for these key checkpoints:

- model loaded:
  - `FALL_CLS: model_size=...`
  - `FALL_CLS: model_addr=0x........ aligned16=1`
  - `FALL_CLS: largest_internal_kb=... largest_psram_kb=...`
  - `FALL_CLS: esp-dl model loaded successfully`
- IMU inference is active:
  - `imu_fall class=<0|1|2> probs=[...]`
- networking is active:
  - `wifi connected ...`
  - `auth ...`
  - `telemetry sent ...`

5. Interpret the IMU classes as:

- `class=0`: `non_fall`
- `class=1`: `pre_impact`
- `class=2`: `fall`

6. Recommended motion test sequence:

- keep the board still or walk normally and confirm it is mostly `class=0`
- do a fast imbalance / crouch / pre-drop motion and check whether `class=1` appears
- simulate a fall or rapid lay-down and check whether `class=2` probability rises clearly

7. If you only want to validate the on-device IMU model, you do not need the full RK3588 link first. The minimum success criteria are:

- `FALL_CLS: esp-dl model loaded successfully`
- repeated `imu_fall class=... probs=[...]` lines after the rolling IMU window fills

Common failure hints:

- no `esp-dl model loaded successfully`:
  - the embedded `.espdl` artifact is missing, empty, misaligned, or PSRAM-backed load failed
- no `imu_fall class=...` lines:
  - the MPU6050 path is not producing data yet, or the rolling 256-sample window is not full
- repeated Wi-Fi / auth retry logs:
  - the local model may still be running, but the RK3588 uplink path is not ready

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

The portal scans nearby APs automatically and refreshes the SSID dropdown every 3 seconds; users only select a discovered SSID and enter the password.

After saving the form, the device stores config in NVS and reboots into STA mode.

While the device is waiting in provisioning mode, serial logs emit a heartbeat every 3 seconds with the current AP client count and latest Wi-Fi scan summary.

## Local verification

```bash
bash tools/run_host_checks.sh
```

This runs the external IMU model tests from `/home/elf/workspace/imu_fall_detect/imu_fall_model/tests`,
the provisioning host-side checks, and then a full `idf.py build`.

## Helpful scripts

- `tools/run_host_checks.sh`: README keyword checks + full build
- `tools/serial_capture.sh`: wraps `idf.py monitor` and saves serial logs
- `tools/push_bundle_to_board.sh`: copies firmware artifacts and docs to the RK3588 side

## More docs

- `docs/esp-firmware-architecture.md`
- `docs/esp-imu-fall-validation-report.md`
- `docs/esp-provisioning-guide.md`

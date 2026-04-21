# ESP Firmware Redesign Design

Date: 2026-04-21
Project: `rk_health_station/esp_fw`
Status: Draft reviewed in conversation, ready for user review

## 1. Background and Goal

`rk_health_station` currently contains only a small set of reusable ESP-side components under `esp_fw/components/`, but it is not yet a complete ESP-IDF firmware project that can be built, flashed, provisioned, and integrated against the RK3588 host as a standalone deliverable.

The RK3588 side already provides a TCP-based `healthd` service, device authentication flow, telemetry ingestion, persistence, and UI display for health metrics. The ESP side now needs to be rebuilt as a complete firmware project inside this repository so the overall system is coherent and self-contained.

The goal of this redesign is to create a standalone ESP-IDF project in `rk_health_station/esp_fw` for `ESP32-S3 + MAX30102 + MPU6050 + dual LEDs`, with:

- real sensor data as the default and primary runtime path
- AP + Web Portal provisioning
- TCP connectivity to RK3588 `healthd`
- protocol-compliant device authentication
- real telemetry upload for heart rate, SpO2, acceleration, and sensor state
- enough diagnostics and logs to support reliable on-board debugging and RK3588 integration

This is a reimplementation guided by current repository requirements and old project experience, not a mechanical migration of an external ESP project.

## 2. Scope

### 2.1 In Scope

- turn `esp_fw` into a complete standalone ESP-IDF project
- implement AP + Web Portal provisioning
- store device configuration in NVS
- connect to Wi-Fi in STA mode and recover from disconnects
- connect to RK3588 `healthd` over TCP using the documented length-prefixed JSON frame protocol
- implement `auth_hello -> auth_challenge -> auth_proof -> auth_result`
- acquire real data from `MAX30102` and `MPU6050`
- compute and upload heart rate, SpO2, acceleration, and sensor state
- expose runtime diagnostics and logs needed for lab bring-up and board-side debugging
- document build, flash, provisioning, and integration steps

### 2.2 Out of Scope for the First Delivery

- OTA upgrades
- BLE provisioning
- cloud uplink or MQTT restoration
- multi-board abstraction beyond the current hardware target
- fully optimized final low-power strategy
- consumer-grade polished configuration UX beyond the required web portal

## 3. Reference Constraints

The redesign must align with the current repository as source of truth:

- protocol: `protocol/device_tcp_protocol.md`
- RK3588 ingress and auth handling: `rk_app/src/healthd/app/daemon_app.cpp`
- telemetry ingestion: `rk_app/src/healthd/telemetry/telemetry_service.cpp`
- persistence: `rk_app/src/shared/storage/database.cpp`
- UI consumption: `rk_app/src/health_ui/pages/dashboard_page.cpp`, `rk_app/src/health_ui/pages/history_page.cpp`

The historical external ESP project may be used only as reference for intent, hardware handling ideas, and algorithm heuristics. It must not remain a required dependency for building or running the firmware.

## 4. Target Architecture

The ESP firmware is organized in five layers:

1. Hardware layer
   - board configuration, I2C, LEDs, sensor drivers
2. Algorithm layer
   - filtering, quality assessment, heart rate / SpO2 calculation, acceleration summarization
3. Device service layer
   - config persistence, diagnostics, provisioning, Wi-Fi state handling
4. Network protocol layer
   - TCP framing, auth state machine, telemetry upload
5. Application orchestration layer
   - startup sequencing, mode transitions, fault recovery, system coordination

The main architectural rule is separation of concerns:

- drivers do not know about TCP or authentication
- algorithms do not know about sockets or provisioning
- networking does not read hardware directly
- `main/` does not contain business logic that belongs in components

## 5. Proposed Directory Layout

```text
esp_fw/
  CMakeLists.txt
  sdkconfig.defaults
  partitions.csv
  README.md
  main/
    app_main.c
    app_controller.c
    app_controller.h
    app_events.h
  components/
    board_config/
    config_store/
    provisioning_portal/
    wifi_manager/
    net_client/
    auth_client/
    telemetry/
    max30102/
    mpu6050/
    heart_rate_algo/
    signal_filter/
    led_status/
    system_diag/
  docs/
    esp-firmware-architecture.md
    esp-provisioning-guide.md
```

Notes:

- `main/` stays thin and orchestrates the system lifecycle.
- `board_config` centralizes pins, I2C selection, timing constants, and board-level defaults.
- `system_diag` contains runtime diagnostics, error snapshots, counters, and structured log helpers.
- existing reusable components can be retained and rewritten as needed, but the final shape must fit the standalone project boundary.

## 6. Module Responsibilities

### 6.1 `board_config`

- define the default pins for `MAX30102`, `MPU6050`, LEDs, and any control GPIOs
- define board-specific timing, sampling, and retry constants
- provide a single include surface for board-level parameters

### 6.2 `config_store`

- own the canonical persisted device configuration in NVS
- store:
  - Wi-Fi SSID / password
  - RK3588 host IP or hostname
  - RK3588 host port
  - `device_id`
  - `device_name`
  - `device_secret`
- validate completeness and basic formatting before reporting configuration as usable

### 6.3 `provisioning_portal`

- expose AP + Web Portal for first-time setup and reconfiguration
- collect all required config in one page
- write validated config into `config_store`
- trigger transition from provisioning mode into normal boot flow

### 6.4 `wifi_manager`

- own the station-mode connection state machine
- surface connected / disconnected / retrying / signal quality states
- handle retry with backoff
- avoid embedding protocol logic

### 6.5 `net_client`

- connect and reconnect to the RK3588 host
- implement length-prefixed frame send/receive
- provide robust socket error handling and timeout reporting

### 6.6 `auth_client`

- implement the documented challenge-response authentication flow
- generate client nonce and HMAC-SHA256 proof
- map backend responses to explicit local auth states:
  - unauthenticated
  - pending approval
  - authenticated
  - rejected

### 6.7 `telemetry`

- own JSON frame assembly for telemetry payloads
- stamp sequence numbers and timestamps
- upload only after successful auth
- manage retry behavior after transient link failures

### 6.8 `max30102`

- initialize the optical sensor
- expose raw red / IR sampling
- report device health and sampling faults

### 6.9 `mpu6050`

- initialize the IMU
- expose acceleration sampling
- report device health and sampling faults

### 6.10 `heart_rate_algo` and `signal_filter`

- transform raw optical samples into quality-scored signal windows
- evaluate finger presence and signal validity
- compute heart rate and SpO2 only from valid windows
- provide compact diagnostics describing why a window was rejected

### 6.11 `led_status`

- drive dual-LED status indication
- support distinct patterns for:
  - boot
  - self-test
  - provisioning
  - Wi-Fi connecting
  - authenticated and sampling
  - pending approval
  - fault

### 6.12 `system_diag`

- provide centralized runtime diagnostics
- keep the last fault reason per subsystem
- maintain counters for reconnects, auth attempts, invalid windows, and upload failures
- support debug snapshots without turning diagnostics into a parallel mock runtime mode

## 7. Runtime Flow

### 7.1 Boot Sequence

1. initialize NVS, logging, LEDs, buses, and board config
2. load configuration from `config_store`
3. if configuration is missing or invalid, enter AP provisioning mode
4. after valid config is present, start Wi-Fi station mode
5. when Wi-Fi is connected, establish TCP to RK3588 `healthd`
6. run device auth handshake
7. once auth succeeds, start normal sensor acquisition and telemetry upload

### 7.2 Steady-State Operation

During normal operation:

- `sensor_task` samples optical and IMU data
- algorithm modules produce validated result snapshots
- `telemetry_task` uploads current results at the configured cadence
- connectivity and auth state are continuously monitored
- LEDs and logs reflect current state

### 7.3 Recovery Paths

- Wi-Fi drop: retry with backoff, do not fake online state
- TCP drop: reconnect and perform auth again
- auth `registration_required`: stay pending and retry later
- auth `rejected`: stop normal telemetry and require reprovisioning or secret reset
- sensor initialization failure: enter fault state
- temporary low-quality signal or no finger detected: keep system online but suppress invalid vital results

## 8. Task Model and Concurrency

The firmware is split into focused tasks:

- `app_task`
  - global lifecycle coordination
  - mode transitions
  - subsystem fault aggregation
- `wifi_task`
  - Wi-Fi connection and reconnection
  - RSSI updates
- `link_task`
  - TCP connect/disconnect handling
  - frame receive/send dispatch
  - auth progression
- `sensor_task`
  - periodic `MAX30102` and `MPU6050` acquisition
  - raw sample buffering
  - quality metric generation
- `telemetry_task`
  - consume result snapshots
  - schedule telemetry upload
  - retry transient failures

Communication rules:

- tasks communicate through queues, event groups, and read-only snapshots
- tasks do not mutate each other's internal state directly
- sensor drivers are not called from networking code
- telemetry consumes computed outputs, not raw driver handles

## 9. Data Model and Telemetry Mapping

Sensor processing is split into three levels:

### 9.1 Raw Level

- MAX30102 red / IR samples
- MPU6050 acceleration samples
- basic device temperature or status if available

### 9.2 Intermediate Level

- filtered waveforms
- peak candidates
- finger-detection result
- motion intensity
- signal quality score

### 9.3 Result Level

- `heart_rate`
- `spo2`
- `acceleration`
- `finger_detected`
- optional firmware/device identity fields already accepted by the protocol

Rules:

- real sensor data is the only formal source for uploaded results
- if no finger is detected, `finger_detected=0` and heart rate / SpO2 must not be treated as valid
- strong motion may still allow acceleration upload while causing heart rate / SpO2 to be skipped for that window

## 10. Real-Sensor-First Diagnostics Strategy

The firmware must prioritize real hardware behavior while still being debuggable on the bench.

Chosen strategy:

- default runtime always uses the real sensor acquisition path
- no normal user-facing runtime mode switch between real and mock data
- diagnostics exist to help isolate faults, not to replace the product path

Required diagnostic categories:

- boot / provisioning
- Wi-Fi / TCP / auth
- sensor initialization and raw-quality summaries
- telemetry cadence and failure reasons

Logging policy:

- default log level: `INFO`
- debug sessions may raise selected modules to `DEBUG`
- prefer compact summaries over unbounded raw sample dumps

Examples of expected diagnostic output:

- sensor init success/failure
- sampling frequency and FIFO anomalies
- finger presence transitions
- valid-window ratio
- heart rate / SpO2 update decisions
- acceleration peaks or motion summaries
- TCP reconnect reason
- auth result and retry state
- telemetry upload success/failure counters

Hidden developer-oriented diagnostic hooks may be kept for isolation and lab verification, but they must remain off by default and must not become a second normal operating mode.

## 11. Error Handling Policy

### 11.1 Configuration Errors

- missing required configuration enters provisioning mode
- invalid persisted configuration is treated as unusable until corrected

### 11.2 Network Errors

- Wi-Fi failures trigger retry with backoff
- TCP failures trigger reconnect and fresh authentication

### 11.3 Authentication Errors

- `registration_required`: pending approval state, periodic retry
- `rejected`: stop normal telemetry, require operator action

### 11.4 Sensor Errors

- bus or initialization failure: fault state
- temporary poor quality window: suppress invalid metrics for that cycle without killing connectivity

### 11.5 Data Validity Errors

- no-finger and poor-quality windows must not produce misleading heart rate or SpO2 uploads
- acceleration reporting may continue independently if its data remains valid

## 12. Provisioning UX

Provisioning uses AP + Web Portal with one-page configuration.

Required fields:

- Wi-Fi SSID
- Wi-Fi password
- RK3588 host IP or hostname
- RK3588 host port
- `device_id`
- `device_name`
- `device_secret`

Behavior:

- first boot with no valid config enters AP mode automatically
- re-entry to provisioning may be triggered through a reset mechanism defined during implementation
- successful form submission persists config and transitions toward normal station-mode operation

## 13. RK3588 Integration Contract

The ESP firmware must remain compatible with the current RK3588 stack:

- connect to the TCP port defined by the deployment configuration, with `19001` as the documented default
- follow the JSON frame envelope described in `protocol/device_tcp_protocol.md`
- authenticate using the existing challenge-response protocol
- send telemetry fields already supported by the backend and UI pipeline

The first delivery should not require protocol redesign on the RK3588 side unless a verified integration defect is found and explicitly approved.

## 14. Validation and Acceptance Criteria

### 14.1 Build Validation

- `esp_fw` builds successfully as a standalone ESP-IDF project on the current development machine
- output artifacts are suitable for flashing the target board

### 14.2 Flash and Boot Validation

- firmware flashes successfully to the target `ESP32-S3`
- serial logs show boot, initialization, provisioning or connection flow, auth, sampling, and upload transitions

### 14.3 Sensor Validation

- `MAX30102` produces stable finger-detection and optical samples
- heart rate and SpO2 are computed from real data under usable conditions
- `MPU6050` produces meaningful acceleration changes with movement

### 14.4 Protocol Validation

- firmware establishes TCP connectivity to RK3588
- firmware completes auth against `healthd`
- telemetry frames continue to upload after auth

### 14.5 Backend Validation

- device records appear on the RK3588 side
- backend stores heart rate, SpO2, acceleration, and related state fields
- dashboard/history at minimum reflect the supported values already wired in the RK3588 application

### 14.6 Diagnostic Validation

- when an issue occurs, logs clearly indicate whether the failure sits in provisioning, Wi-Fi, TCP, auth, sensor, or algorithm stages

## 15. Delivery Phasing

The execution plan derived from this design should still follow phased delivery internally:

1. make `esp_fw` a complete standalone ESP-IDF project
2. complete provisioning, config persistence, Wi-Fi, TCP, and auth path
3. complete real sensor acquisition and algorithm pipeline
4. complete board flash, RK3588 integration, diagnostics, and documentation

Although development is phased, acceptance is for the whole working system, not for an isolated partial prototype.

## 16. Non-Goals and Guardrails

- do not keep `esp_health_watch` as a required build root
- do not create a permanent product-level mock/real runtime toggle
- do not overload `main/` with driver or protocol code
- do not silently upload invalid vital signs when data quality is poor
- do not broaden scope into unrelated platform ambitions before the first complete end-to-end delivery works

## 17. Open Implementation Notes

These are implementation notes, not unresolved requirements:

- exact driver API details, queue depths, and task priorities should be decided during planning and implementation
- the reset path for re-entering provisioning should be chosen to match available hardware controls
- sensor algorithm tuning may require board-side iteration, but the architectural boundaries defined here should not change

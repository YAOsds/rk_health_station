# ESP Firmware Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild `rk_health_station/esp_fw` into a standalone ESP-IDF firmware for `ESP32-S3 + MAX30102 + MPU6050 + dual LEDs` that supports AP provisioning, Wi-Fi, TCP auth, and real telemetry upload to RK3588 `healthd`.

**Architecture:** The implementation keeps `main/` thin and moves behavior into focused components for board configuration, diagnostics, provisioning, connectivity, auth, telemetry, sensors, and algorithms. Development proceeds bottom-up with testable pure helpers first, then integration wiring, then on-board and RK3588 end-to-end validation.

**Tech Stack:** ESP-IDF 6.x, FreeRTOS, ESP HTTP server / Wi-Fi, NVS, lwIP sockets, mbedTLS HMAC-SHA256, cJSON, Unity test framework, MAX30102 and MPU6050 I2C drivers.

---

## File Structure Map

### Top-level project files
- Create: `esp_fw/CMakeLists.txt` - standalone ESP-IDF project entrypoint and component registration.
- Create: `esp_fw/sdkconfig.defaults` - default Kconfig values for Wi-Fi, HTTP server, mbedTLS, logging, and target settings.
- Create: `esp_fw/partitions.csv` - NVS, PHY init, factory app layout sized for provisioning plus diagnostics.
- Modify: `esp_fw/README.md` - replace migration note with real build/flash/provision/debug guide.
- Create: `esp_fw/docs/esp-firmware-architecture.md` - architecture notes aligned with the approved spec.
- Create: `esp_fw/docs/esp-provisioning-guide.md` - operator steps for AP setup and RK3588 pairing.

### Main orchestration
- Create: `esp_fw/main/CMakeLists.txt`
- Create: `esp_fw/main/app_main.c`
- Create: `esp_fw/main/app_controller.h`
- Create: `esp_fw/main/app_controller.c`
- Create: `esp_fw/main/app_events.h`

### Board and diagnostics
- Create: `esp_fw/components/board_config/CMakeLists.txt`
- Create: `esp_fw/components/board_config/include/board_config.h`
- Create: `esp_fw/components/board_config/board_config.c`
- Create: `esp_fw/components/system_diag/CMakeLists.txt`
- Create: `esp_fw/components/system_diag/include/system_diag.h`
- Create: `esp_fw/components/system_diag/system_diag.c`
- Create: `esp_fw/components/system_diag/test/test_system_diag.c`

### Config and provisioning
- Modify: `esp_fw/components/config_store/CMakeLists.txt`
- Modify: `esp_fw/components/config_store/device_config.h`
- Modify: `esp_fw/components/config_store/device_config.c`
- Create: `esp_fw/components/config_store/test/test_device_config.c`
- Create: `esp_fw/components/provisioning_portal/CMakeLists.txt`
- Create: `esp_fw/components/provisioning_portal/include/provisioning_portal.h`
- Create: `esp_fw/components/provisioning_portal/provisioning_portal.c`
- Create: `esp_fw/components/provisioning_portal/provisioning_html.h`
- Create: `esp_fw/components/provisioning_portal/test/test_provisioning_parser.c`

### Connectivity and protocol
- Create: `esp_fw/components/wifi_manager/CMakeLists.txt`
- Create: `esp_fw/components/wifi_manager/include/wifi_manager.h`
- Create: `esp_fw/components/wifi_manager/wifi_manager.c`
- Modify: `esp_fw/components/net_client/CMakeLists.txt`
- Modify: `esp_fw/components/net_client/tcp_client.h`
- Modify: `esp_fw/components/net_client/tcp_client.c`
- Create: `esp_fw/components/net_client/test/test_frame_codec.c`
- Modify: `esp_fw/components/auth_client/CMakeLists.txt`
- Modify: `esp_fw/components/auth_client/auth_client.h`
- Modify: `esp_fw/components/auth_client/auth_client.c`
- Create: `esp_fw/components/auth_client/test/test_auth_client_codec.c`
- Modify: `esp_fw/components/telemetry/CMakeLists.txt`
- Modify: `esp_fw/components/telemetry/telemetry_uploader.h`
- Modify: `esp_fw/components/telemetry/telemetry_uploader.c`
- Create: `esp_fw/components/telemetry/test/test_telemetry_encoder.c`

### Sensors, algorithms, LEDs
- Create: `esp_fw/components/max30102/CMakeLists.txt`
- Create: `esp_fw/components/max30102/include/max30102.h`
- Create: `esp_fw/components/max30102/max30102.c`
- Create: `esp_fw/components/max30102/test/test_max30102_registers.c`
- Create: `esp_fw/components/mpu6050/CMakeLists.txt`
- Create: `esp_fw/components/mpu6050/include/mpu6050.h`
- Create: `esp_fw/components/mpu6050/mpu6050.c`
- Create: `esp_fw/components/mpu6050/test/test_mpu6050_scaling.c`
- Create: `esp_fw/components/signal_filter/CMakeLists.txt`
- Create: `esp_fw/components/signal_filter/include/signal_filter.h`
- Create: `esp_fw/components/signal_filter/signal_filter.c`
- Create: `esp_fw/components/signal_filter/test/test_signal_filter.c`
- Create: `esp_fw/components/heart_rate_algo/CMakeLists.txt`
- Create: `esp_fw/components/heart_rate_algo/include/heart_rate_algo.h`
- Create: `esp_fw/components/heart_rate_algo/heart_rate_algo.c`
- Create: `esp_fw/components/heart_rate_algo/test/test_heart_rate_algo.c`
- Create: `esp_fw/components/led_status/CMakeLists.txt`
- Create: `esp_fw/components/led_status/include/led_status.h`
- Create: `esp_fw/components/led_status/led_status.c`
- Create: `esp_fw/components/led_status/test/test_led_status.c`

### Validation helpers
- Create: `esp_fw/tools/run_host_checks.sh` - repeatable local build/test wrapper.
- Create: `esp_fw/tools/serial_capture.sh` - capture ESP boot and runtime logs during board testing.
- Create: `esp_fw/tools/push_bundle_to_board.sh` - copy firmware artifacts and docs to the RK3588 side if needed for coordinated testing.

---

### Task 1: Bootstrap the standalone ESP-IDF project and diagnostics backbone

**Files:**
- Create: `esp_fw/CMakeLists.txt`
- Create: `esp_fw/sdkconfig.defaults`
- Create: `esp_fw/partitions.csv`
- Create: `esp_fw/main/CMakeLists.txt`
- Create: `esp_fw/main/app_main.c`
- Create: `esp_fw/main/app_controller.h`
- Create: `esp_fw/main/app_controller.c`
- Create: `esp_fw/main/app_events.h`
- Create: `esp_fw/components/board_config/CMakeLists.txt`
- Create: `esp_fw/components/board_config/include/board_config.h`
- Create: `esp_fw/components/board_config/board_config.c`
- Create: `esp_fw/components/system_diag/CMakeLists.txt`
- Create: `esp_fw/components/system_diag/include/system_diag.h`
- Create: `esp_fw/components/system_diag/system_diag.c`
- Test: `esp_fw/components/system_diag/test/test_system_diag.c`

- [ ] **Step 1: Write the failing test**

```c
#include "unity.h"
#include "system_diag.h"

void test_system_diag_defaults_to_clear_state(void)
{
    system_diag_snapshot_t snapshot;

    system_diag_init();
    TEST_ASSERT_EQUAL(ESP_OK, system_diag_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(SYSTEM_DIAG_STAGE_BOOT, snapshot.stage);
    TEST_ASSERT_EQUAL(0, snapshot.wifi_retries);
    TEST_ASSERT_EQUAL(0, snapshot.auth_failures);
    TEST_ASSERT_EQUAL_STRING("", snapshot.last_error);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: FAIL with missing `system_diag.h` / missing project root files.

- [ ] **Step 3: Write minimal implementation**

```c
// esp_fw/main/app_main.c
#include "app_controller.h"

void app_main(void)
{
    app_controller_run();
}

// esp_fw/main/app_controller.c
#include "app_controller.h"
#include "board_config.h"
#include "esp_log.h"
#include "system_diag.h"

static const char *TAG = "APP_CTRL";

void app_controller_run(void)
{
    board_config_init();
    system_diag_init();
    system_diag_set_stage(SYSTEM_DIAG_STAGE_BOOT);
    ESP_LOGI(TAG, "esp_fw bootstrap ready");
}

// esp_fw/components/system_diag/include/system_diag.h
#pragma once
#include "esp_err.h"

typedef enum {
    SYSTEM_DIAG_STAGE_BOOT = 0,
    SYSTEM_DIAG_STAGE_PROVISIONING,
    SYSTEM_DIAG_STAGE_WIFI,
    SYSTEM_DIAG_STAGE_AUTH,
    SYSTEM_DIAG_STAGE_STREAMING,
    SYSTEM_DIAG_STAGE_FAULT,
} system_diag_stage_t;

typedef struct {
    system_diag_stage_t stage;
    int wifi_retries;
    int auth_failures;
    char last_error[96];
} system_diag_snapshot_t;

void system_diag_init(void);
void system_diag_set_stage(system_diag_stage_t stage);
esp_err_t system_diag_get_snapshot(system_diag_snapshot_t *snapshot);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: PASS and produce `build/rk_health_station_esp_fw.bin` (or the configured app binary name) with no missing project-root errors.

- [ ] **Step 5: Commit**

```bash
git add esp_fw/CMakeLists.txt esp_fw/sdkconfig.defaults esp_fw/partitions.csv esp_fw/main esp_fw/components/board_config esp_fw/components/system_diag
git commit -m "feat: bootstrap standalone esp firmware project"
```

### Task 2: Rebuild config storage and AP provisioning around NVS

**Files:**
- Modify: `esp_fw/components/config_store/CMakeLists.txt`
- Modify: `esp_fw/components/config_store/device_config.h`
- Modify: `esp_fw/components/config_store/device_config.c`
- Test: `esp_fw/components/config_store/test/test_device_config.c`
- Create: `esp_fw/components/provisioning_portal/CMakeLists.txt`
- Create: `esp_fw/components/provisioning_portal/include/provisioning_portal.h`
- Create: `esp_fw/components/provisioning_portal/provisioning_portal.c`
- Create: `esp_fw/components/provisioning_portal/provisioning_html.h`
- Test: `esp_fw/components/provisioning_portal/test/test_provisioning_parser.c`
- Modify: `esp_fw/main/app_controller.c`

- [ ] **Step 1: Write the failing tests**

```c
#include "unity.h"
#include "device_config.h"

void test_device_config_requires_all_fields(void)
{
    rk_device_config_t config = {0};

    strcpy(config.wifi_ssid, "lab-ap");
    strcpy(config.device_id, "watch_001");
    TEST_ASSERT_FALSE(rk_device_config_is_complete(&config));
}

void test_device_config_accepts_complete_form(void)
{
    rk_device_config_t config = {0};

    strcpy(config.wifi_ssid, "lab-ap");
    strcpy(config.wifi_password, "12345678");
    strcpy(config.server_ip, "192.168.137.1");
    config.server_port = 19001;
    strcpy(config.device_id, "watch_001");
    strcpy(config.device_name, "RK Watch 01");
    strcpy(config.device_secret, "topsecret");
    TEST_ASSERT_TRUE(rk_device_config_is_complete(&config));
}
```

```c
#include "unity.h"
#include "provisioning_portal.h"

void test_parse_form_body_maps_required_fields(void)
{
    const char *body = "wifi_ssid=lab-ap&wifi_password=12345678&server_ip=192.168.137.1&server_port=19001&device_id=watch_001&device_name=RK+Watch+01&device_secret=topsecret";
    rk_device_config_t config = {0};

    TEST_ASSERT_EQUAL(ESP_OK, provisioning_portal_parse_form(body, &config));
    TEST_ASSERT_EQUAL_STRING("RK Watch 01", config.device_name);
    TEST_ASSERT_EQUAL(19001, config.server_port);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: FAIL because `rk_device_config_is_complete()` and `provisioning_portal_parse_form()` do not exist yet.

- [ ] **Step 3: Write minimal implementation**

```c
// esp_fw/components/config_store/device_config.h
bool rk_device_config_is_complete(const rk_device_config_t *config);
esp_err_t rk_device_config_load(rk_device_config_t *config);
esp_err_t rk_device_config_save(const rk_device_config_t *config);

// esp_fw/components/config_store/device_config.c
bool rk_device_config_is_complete(const rk_device_config_t *config)
{
    return config != NULL
        && config->wifi_ssid[0] != '\0'
        && config->wifi_password[0] != '\0'
        && config->server_ip[0] != '\0'
        && config->server_port > 0
        && config->device_id[0] != '\0'
        && config->device_name[0] != '\0'
        && config->device_secret[0] != '\0';
}

// esp_fw/components/provisioning_portal/include/provisioning_portal.h
esp_err_t provisioning_portal_start(void);
esp_err_t provisioning_portal_stop(void);
esp_err_t provisioning_portal_parse_form(const char *body, rk_device_config_t *config);
```

- [ ] **Step 4: Expand implementation to persistence and portal wiring**

```c
// esp_fw/main/app_controller.c
rk_device_config_t config = {0};
if (rk_device_config_load(&config) != ESP_OK || !rk_device_config_is_complete(&config)) {
    system_diag_set_stage(SYSTEM_DIAG_STAGE_PROVISIONING);
    ESP_ERROR_CHECK(provisioning_portal_start());
    return;
}
```

```c
// esp_fw/components/provisioning_portal/provisioning_portal.c
static esp_err_t handle_submit(httpd_req_t *req)
{
    char body[512];
    rk_device_config_t config = {0};

    httpd_req_recv(req, body, sizeof(body) - 1);
    ESP_RETURN_ON_ERROR(provisioning_portal_parse_form(body, &config), TAG, "invalid form");
    ESP_RETURN_ON_ERROR(rk_device_config_save(&config), TAG, "save failed");
    httpd_resp_sendstr(req, "saved, rebooting into station mode");
    return ESP_OK;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: PASS with NVS and HTTP server symbols resolved.

- [ ] **Step 6: Commit**

```bash
git add esp_fw/components/config_store esp_fw/components/provisioning_portal esp_fw/main/app_controller.c
git commit -m "feat: add nvs config store and ap provisioning portal"
```

### Task 3: Add Wi-Fi manager and resilient TCP frame transport

**Files:**
- Create: `esp_fw/components/wifi_manager/CMakeLists.txt`
- Create: `esp_fw/components/wifi_manager/include/wifi_manager.h`
- Create: `esp_fw/components/wifi_manager/wifi_manager.c`
- Modify: `esp_fw/components/net_client/CMakeLists.txt`
- Modify: `esp_fw/components/net_client/tcp_client.h`
- Modify: `esp_fw/components/net_client/tcp_client.c`
- Test: `esp_fw/components/net_client/test/test_frame_codec.c`
- Modify: `esp_fw/main/app_controller.c`
- Modify: `esp_fw/components/system_diag/system_diag.c`

- [ ] **Step 1: Write the failing test**

```c
#include "unity.h"
#include "tcp_client.h"

void test_frame_header_uses_big_endian_length(void)
{
    uint8_t header[4] = {0};

    tcp_client_encode_frame_header(0x00000123, header);
    TEST_ASSERT_EQUAL_HEX8(0x00, header[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, header[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, header[2]);
    TEST_ASSERT_EQUAL_HEX8(0x23, header[3]);
}

void test_frame_header_rejects_oversized_payload(void)
{
    uint8_t header[4] = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, tcp_client_encode_frame_checked(SIZE_MAX, header));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: FAIL because frame helper APIs and `wifi_manager` symbols are missing.

- [ ] **Step 3: Write minimal implementation**

```c
// esp_fw/components/net_client/tcp_client.h
void tcp_client_encode_frame_header(size_t len, uint8_t header[4]);
esp_err_t tcp_client_encode_frame_checked(size_t len, uint8_t header[4]);

// esp_fw/components/net_client/tcp_client.c
void tcp_client_encode_frame_header(size_t len, uint8_t header[4])
{
    header[0] = (uint8_t)((len >> 24) & 0xff);
    header[1] = (uint8_t)((len >> 16) & 0xff);
    header[2] = (uint8_t)((len >> 8) & 0xff);
    header[3] = (uint8_t)(len & 0xff);
}

esp_err_t tcp_client_encode_frame_checked(size_t len, uint8_t header[4])
{
    if (len == 0 || len > UINT32_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    tcp_client_encode_frame_header(len, header);
    return ESP_OK;
}
```

- [ ] **Step 4: Add Wi-Fi state manager and reconnect path**

```c
// esp_fw/components/wifi_manager/include/wifi_manager.h
esp_err_t wifi_manager_start(const rk_device_config_t *config);
bool wifi_manager_is_connected(void);
int wifi_manager_get_rssi(void);

// esp_fw/main/app_controller.c
ESP_ERROR_CHECK(wifi_manager_start(&config));
system_diag_set_stage(SYSTEM_DIAG_STAGE_WIFI);
if (!wifi_manager_is_connected()) {
    system_diag_note_wifi_retry();
    return;
}
ESP_ERROR_CHECK(tcp_client_configure(config.server_ip, (uint16_t)config.server_port));
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: PASS and logs show frame helper tests compiling alongside Wi-Fi event glue.

- [ ] **Step 6: Commit**

```bash
git add esp_fw/components/wifi_manager esp_fw/components/net_client esp_fw/main/app_controller.c esp_fw/components/system_diag/system_diag.c
git commit -m "feat: add wifi manager and robust tcp framing"
```

### Task 4: Refactor auth and telemetry into testable protocol components

**Files:**
- Modify: `esp_fw/components/auth_client/CMakeLists.txt`
- Modify: `esp_fw/components/auth_client/auth_client.h`
- Modify: `esp_fw/components/auth_client/auth_client.c`
- Test: `esp_fw/components/auth_client/test/test_auth_client_codec.c`
- Modify: `esp_fw/components/telemetry/CMakeLists.txt`
- Modify: `esp_fw/components/telemetry/telemetry_uploader.h`
- Modify: `esp_fw/components/telemetry/telemetry_uploader.c`
- Test: `esp_fw/components/telemetry/test/test_telemetry_encoder.c`
- Modify: `esp_fw/main/app_controller.c`

- [ ] **Step 1: Write the failing tests**

```c
#include "unity.h"
#include "auth_client.h"

void test_auth_proof_builder_matches_protocol_contract(void)
{
    rk_device_config_t config = {0};
    char proof[65] = {0};

    strcpy(config.device_id, "watch_001");
    strcpy(config.device_secret, "secret123");
    TEST_ASSERT_EQUAL(ESP_OK, auth_client_build_proof(&config, "srv_nonce", "cli_nonce", 1713000001, proof, sizeof(proof)));
    TEST_ASSERT_EQUAL(64, strlen(proof));
}
```

```c
#include "unity.h"
#include "telemetry_uploader.h"

void test_telemetry_encoder_includes_required_fields(void)
{
    telemetry_vitals_t vitals = {
        .heart_rate = 72,
        .spo2 = 98.4f,
        .acceleration = 0.42f,
        .finger_detected = true,
    };
    char frame[512] = {0};

    TEST_ASSERT_EQUAL(ESP_OK, telemetry_uploader_build_frame("watch_001", "RK Watch 01", "1.0.0-rk", 12, 1713000010, &vitals, frame, sizeof(frame)));
    TEST_ASSERT_NOT_NULL(strstr(frame, "\"type\":\"telemetry_batch\""));
    TEST_ASSERT_NOT_NULL(strstr(frame, "\"heart_rate\":72"));
    TEST_ASSERT_NOT_NULL(strstr(frame, "\"finger_detected\":1"));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: FAIL because the pure helper APIs do not exist and `auth_client` still depends on the old shape.

- [ ] **Step 3: Write minimal implementation**

```c
// esp_fw/components/auth_client/auth_client.h
typedef enum {
    AUTH_STATE_UNAUTHENTICATED = 0,
    AUTH_STATE_PENDING,
    AUTH_STATE_AUTHENTICATED,
    AUTH_STATE_REJECTED,
} auth_client_state_t;

esp_err_t auth_client_build_proof(const rk_device_config_t *config, const char *server_nonce,
    const char *client_nonce, int64_t ts, char *proof_hex, size_t proof_hex_len);
```

```c
// esp_fw/components/telemetry/telemetry_uploader.h
typedef struct {
    int heart_rate;
    float spo2;
    float acceleration;
    bool finger_detected;
} telemetry_vitals_t;

esp_err_t telemetry_uploader_build_frame(const char *device_id, const char *device_name,
    const char *firmware_version, uint32_t seq, int64_t ts,
    const telemetry_vitals_t *vitals, char *buffer, size_t buffer_len);
```

- [ ] **Step 4: Rewire live auth and upload path**

```c
// esp_fw/main/app_controller.c
auth_client_state_t auth_state = AUTH_STATE_UNAUTHENTICATED;
ESP_ERROR_CHECK(auth_client_authenticate(&config, APP_FIRMWARE_VERSION, &auth_state));
if (auth_state == AUTH_STATE_PENDING) {
    system_diag_note_auth_pending();
    return;
}
if (auth_state != AUTH_STATE_AUTHENTICATED) {
    system_diag_note_auth_failure("auth_result_not_ok");
    return;
}
```

```c
// esp_fw/components/telemetry/telemetry_uploader.c
telemetry_vitals_t vitals = {
    .heart_rate = snapshot.heart_rate,
    .spo2 = snapshot.spo2,
    .acceleration = snapshot.acceleration,
    .finger_detected = snapshot.finger_detected,
};
ESP_RETURN_ON_ERROR(telemetry_uploader_build_frame(config->device_id, config->device_name,
    s_firmware_version, s_next_seq++, now_ts, &vitals, frame, sizeof(frame)), TAG, "encode failed");
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: PASS and the old `json` dependency issue is fixed by linking against `json` / `cjson` correctly for ESP-IDF 6.x.

- [ ] **Step 6: Commit**

```bash
git add esp_fw/components/auth_client esp_fw/components/telemetry esp_fw/main/app_controller.c
git commit -m "feat: refactor auth and telemetry protocol pipeline"
```

### Task 5: Implement MAX30102, MPU6050, signal filtering, and vital-sign algorithms

**Files:**
- Create: `esp_fw/components/max30102/CMakeLists.txt`
- Create: `esp_fw/components/max30102/include/max30102.h`
- Create: `esp_fw/components/max30102/max30102.c`
- Test: `esp_fw/components/max30102/test/test_max30102_registers.c`
- Create: `esp_fw/components/mpu6050/CMakeLists.txt`
- Create: `esp_fw/components/mpu6050/include/mpu6050.h`
- Create: `esp_fw/components/mpu6050/mpu6050.c`
- Test: `esp_fw/components/mpu6050/test/test_mpu6050_scaling.c`
- Create: `esp_fw/components/signal_filter/CMakeLists.txt`
- Create: `esp_fw/components/signal_filter/include/signal_filter.h`
- Create: `esp_fw/components/signal_filter/signal_filter.c`
- Test: `esp_fw/components/signal_filter/test/test_signal_filter.c`
- Create: `esp_fw/components/heart_rate_algo/CMakeLists.txt`
- Create: `esp_fw/components/heart_rate_algo/include/heart_rate_algo.h`
- Create: `esp_fw/components/heart_rate_algo/heart_rate_algo.c`
- Test: `esp_fw/components/heart_rate_algo/test/test_heart_rate_algo.c`
- Modify: `esp_fw/components/system_diag/include/system_diag.h`
- Modify: `esp_fw/components/system_diag/system_diag.c`

- [ ] **Step 1: Write the failing tests**

```c
#include "unity.h"
#include "signal_filter.h"

void test_signal_filter_detects_finger_presence_from_ir_dc_level(void)
{
    signal_filter_window_t window = {
        .sample_count = 4,
        .ir_samples = {12000, 11800, 12100, 11950},
        .red_samples = {9800, 9700, 9900, 9850},
    };
    signal_quality_t quality = {0};

    TEST_ASSERT_EQUAL(ESP_OK, signal_filter_analyze_window(&window, &quality));
    TEST_ASSERT_TRUE(quality.finger_detected);
}
```

```c
#include "unity.h"
#include "heart_rate_algo.h"

void test_heart_rate_algo_rejects_low_quality_window(void)
{
    signal_quality_t quality = {
        .finger_detected = false,
        .confidence = 0.1f,
    };
    heart_rate_result_t result = {0};

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, heart_rate_algo_compute(&quality, &result));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: FAIL because sensor and algorithm components do not exist yet.

- [ ] **Step 3: Write minimal implementation**

```c
// esp_fw/components/signal_filter/include/signal_filter.h
typedef struct {
    size_t sample_count;
    uint32_t ir_samples[128];
    uint32_t red_samples[128];
} signal_filter_window_t;

typedef struct {
    bool finger_detected;
    float confidence;
    float dc_ir;
    float dc_red;
    float motion_level;
} signal_quality_t;

esp_err_t signal_filter_analyze_window(const signal_filter_window_t *window, signal_quality_t *quality);
```

```c
// esp_fw/components/heart_rate_algo/include/heart_rate_algo.h
typedef struct {
    int heart_rate_bpm;
    float spo2_percent;
} heart_rate_result_t;

esp_err_t heart_rate_algo_compute(const signal_quality_t *quality, heart_rate_result_t *result);
```

- [ ] **Step 4: Add driver and algorithm integration hooks**

```c
// esp_fw/components/max30102/include/max30102.h
esp_err_t max30102_init(i2c_port_t port);
esp_err_t max30102_read_fifo(signal_filter_window_t *window);

// esp_fw/components/mpu6050/include/mpu6050.h
typedef struct {
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float accel_norm_g;
} mpu6050_sample_t;

esp_err_t mpu6050_init(i2c_port_t port);
esp_err_t mpu6050_read_sample(mpu6050_sample_t *sample);
```

```c
// esp_fw/components/system_diag/system_diag.c
void system_diag_note_signal_quality(float confidence, bool finger_detected, float motion_level)
{
    s_snapshot.signal_confidence = confidence;
    s_snapshot.finger_detected = finger_detected;
    s_snapshot.motion_level = motion_level;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: PASS with sensor and algorithm components compiling cleanly.

- [ ] **Step 6: Commit**

```bash
git add esp_fw/components/max30102 esp_fw/components/mpu6050 esp_fw/components/signal_filter esp_fw/components/heart_rate_algo esp_fw/components/system_diag
git commit -m "feat: add sensor drivers and vital sign algorithms"
```

### Task 6: Add LED state indication and full application task orchestration

**Files:**
- Create: `esp_fw/components/led_status/CMakeLists.txt`
- Create: `esp_fw/components/led_status/include/led_status.h`
- Create: `esp_fw/components/led_status/led_status.c`
- Test: `esp_fw/components/led_status/test/test_led_status.c`
- Modify: `esp_fw/main/app_events.h`
- Modify: `esp_fw/main/app_controller.h`
- Modify: `esp_fw/main/app_controller.c`
- Modify: `esp_fw/main/app_main.c`
- Modify: `esp_fw/components/system_diag/include/system_diag.h`

- [ ] **Step 1: Write the failing test**

```c
#include "unity.h"
#include "led_status.h"

void test_led_status_maps_pending_auth_to_distinct_pattern(void)
{
    led_pattern_t pattern = {0};

    TEST_ASSERT_EQUAL(ESP_OK, led_status_get_pattern(LED_STATUS_PENDING_APPROVAL, &pattern));
    TEST_ASSERT_GREATER_THAN_UINT32(0, pattern.on_ms);
    TEST_ASSERT_GREATER_THAN_UINT32(0, pattern.off_ms);
    TEST_ASSERT_TRUE(pattern.repeat);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: FAIL because LED component and final app event loop are not implemented yet.

- [ ] **Step 3: Write minimal implementation**

```c
// esp_fw/components/led_status/include/led_status.h
typedef enum {
    LED_STATUS_BOOT = 0,
    LED_STATUS_PROVISIONING,
    LED_STATUS_WIFI_CONNECTING,
    LED_STATUS_PENDING_APPROVAL,
    LED_STATUS_STREAMING,
    LED_STATUS_FAULT,
} led_status_state_t;

typedef struct {
    uint32_t on_ms;
    uint32_t off_ms;
    bool repeat;
} led_pattern_t;

esp_err_t led_status_init(void);
esp_err_t led_status_set(led_status_state_t state);
esp_err_t led_status_get_pattern(led_status_state_t state, led_pattern_t *pattern);
```

- [ ] **Step 4: Wire the FreeRTOS tasks and event loop**

```c
// esp_fw/main/app_controller.h
void app_controller_run(void);

// esp_fw/main/app_controller.c
static void sensor_task(void *arg);
static void telemetry_task(void *arg);
static void link_task(void *arg);

void app_controller_run(void)
{
    led_status_init();
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    xTaskCreate(link_task, "link_task", 4096, NULL, 6, NULL);
    xTaskCreate(telemetry_task, "telemetry_task", 4096, NULL, 5, NULL);
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py build'`
Expected: PASS with LED states and orchestration tasks linked into the application.

- [ ] **Step 6: Commit**

```bash
git add esp_fw/components/led_status esp_fw/main/app_events.h esp_fw/main/app_controller.h esp_fw/main/app_controller.c esp_fw/main/app_main.c esp_fw/components/system_diag/include/system_diag.h
git commit -m "feat: orchestrate runtime tasks and led state signaling"
```

### Task 7: Finish docs, automation scripts, and local build verification

**Files:**
- Modify: `esp_fw/README.md`
- Create: `esp_fw/docs/esp-firmware-architecture.md`
- Create: `esp_fw/docs/esp-provisioning-guide.md`
- Create: `esp_fw/tools/run_host_checks.sh`
- Create: `esp_fw/tools/serial_capture.sh`
- Create: `esp_fw/tools/push_bundle_to_board.sh`

- [ ] **Step 1: Write the failing test**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

grep -q "idf.py build" README.md
grep -q "AP provisioning" README.md
grep -q "serial_capture.sh" README.md
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash esp_fw/tools/run_host_checks.sh`
Expected: FAIL because the helper scripts and expanded operator docs do not exist yet.

- [ ] **Step 3: Write minimal implementation**

```bash
# esp_fw/tools/run_host_checks.sh
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. ~/esp/esp-idf/export.sh >/tmp/esp_export.log
idf.py build
```

```markdown
# esp_fw/README.md
## Build
```bash
cd esp_fw
. ~/esp/esp-idf/export.sh
idf.py build
```

## Provisioning
- Power on the ESP32-S3.
- Join the AP exposed by the device.
- Open the portal and fill Wi-Fi, RK3588 host, port, `device_id`, `device_name`, and `device_secret`.
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash esp_fw/tools/run_host_checks.sh`
Expected: PASS with a full local build and README keyword checks succeeding.

- [ ] **Step 5: Commit**

```bash
git add esp_fw/README.md esp_fw/docs esp_fw/tools
git commit -m "docs: add esp firmware build and debug guides"
```

### Task 8: Perform board flash and RK3588 end-to-end validation

**Files:**
- Modify: `esp_fw/README.md`
- Modify: `esp_fw/docs/esp-provisioning-guide.md`
- Modify: `esp_fw/tools/serial_capture.sh`
- Modify: `esp_fw/tools/push_bundle_to_board.sh`
- Create: `esp_fw/docs/esp-rk3588-validation-report.md`

- [ ] **Step 1: Write the failing validation checklist**

```markdown
# Validation checklist
- [ ] ESP32-S3 flashes successfully with `idf.py -p /dev/ttyACM0 flash monitor`
- [ ] AP portal accepts Wi-Fi + RK3588 settings and reboots into STA mode
- [ ] Serial log shows Wi-Fi connected, TCP connected, auth_result ok or registration_required
- [ ] Real MAX30102 finger detection toggles when finger is placed/removed
- [ ] Real MPU6050 acceleration changes with board movement
- [ ] RK3588 `healthd` receives telemetry and stores heart_rate/spo2/acceleration
```

- [ ] **Step 2: Run the failing end-to-end validation**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py -p /dev/ttyACM0 flash monitor'`
Expected: FAIL initially on at least one unchecked item until all previous tasks are completed and hardware is connected.

- [ ] **Step 3: Complete the validation flow**

```bash
# Capture ESP logs
bash esp_fw/tools/serial_capture.sh /dev/ttyACM0 /tmp/esp_fw-session.log

# On RK3588 host, verify auth and telemetry ingestion
ssh 192.168.137.179 'journalctl --user -u healthd --since "5 minutes ago" --no-pager || tail -n 200 ~/.local/state/rk_health_station/healthd.log'
```

```markdown
# esp_fw/docs/esp-rk3588-validation-report.md
## Result
- Board flashed on target ESP32-S3
- Provisioning completed against lab Wi-Fi
- RK3588 host reachable at configured IP and port
- Auth outcome observed: `ok` or `registration_required` with follow-up approval steps recorded
- Telemetry fields observed: `heart_rate`, `spo2`, `acceleration`, `finger_detected`
- Outstanding issues, if any, listed with exact serial and host log excerpts
```

- [ ] **Step 4: Re-run validation to verify it passes**

Run: `bash -lc 'cd esp_fw && . ~/esp/esp-idf/export.sh >/tmp/esp_export.log && idf.py -p /dev/ttyACM0 flash monitor'`
Expected: PASS on the checklist above, with evidence captured in `esp_fw/docs/esp-rk3588-validation-report.md`.

- [ ] **Step 5: Commit**

```bash
git add esp_fw/README.md esp_fw/docs/esp-provisioning-guide.md esp_fw/docs/esp-rk3588-validation-report.md esp_fw/tools/serial_capture.sh esp_fw/tools/push_bundle_to_board.sh
git commit -m "test: validate esp firmware against rk3588 host"
```

---

## Self-Review

### Spec coverage check
- Standalone ESP-IDF project: Task 1
- NVS config + AP portal: Task 2
- Wi-Fi + TCP transport: Task 3
- Auth + telemetry protocol: Task 4
- Real sensors + algorithms: Task 5
- LED states + orchestration: Task 6
- Docs and build/debug scripts: Task 7
- Flash, board test, RK3588 integration: Task 8

### Placeholder scan
- Checked for `TODO`, `TBD`, `implement later`, and `similar to` placeholders: none intentionally left.
- Every task includes exact paths, concrete code snippets, concrete commands, and an explicit commit step.

### Type consistency check
- `rk_device_config_t`, `system_diag_snapshot_t`, `auth_client_state_t`, `telemetry_vitals_t`, `signal_quality_t`, and `heart_rate_result_t` are named consistently across later tasks.
- `app_controller_run()` remains the single main entrypoint across all tasks.

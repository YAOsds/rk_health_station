# Unified Runtime Configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace user-facing runtime env-var setup and scattered hard-coded runtime values with one JSON config file plus a standalone `health-config-ui`, while keeping env overrides for advanced debug and board experiments.

**Architecture:** Add a shared runtime-config library under `rk_app/src/shared/runtime_config/` that owns defaults, JSON parsing, validation, path expansion, and env overrides. Inject typed config into `healthd`, `health-videod`, `health-falld`, `health-ui`, and a new `health-config-ui`; then package the JSON file and config launcher into the RK3588 bundle.

**Tech Stack:** C++17, Qt Core/Widgets/Test, QJsonDocument/QJsonObject, existing CMake/CTest, bundle shell scripts.

---

### Task 1: Add the shared runtime-config core and loader tests

**Files:**
- Create: `rk_app/src/shared/runtime_config/app_runtime_config.h`
- Create: `rk_app/src/shared/runtime_config/app_runtime_config.cpp`
- Create: `rk_app/src/shared/runtime_config/app_runtime_config_loader.h`
- Create: `rk_app/src/shared/runtime_config/app_runtime_config_loader.cpp`
- Create: `rk_app/src/shared/runtime_config/app_runtime_config_validator.h`
- Create: `rk_app/src/shared/runtime_config/app_runtime_config_validator.cpp`
- Create: `rk_app/src/shared/runtime_config/app_runtime_config_paths.h`
- Create: `rk_app/src/shared/runtime_config/app_runtime_config_paths.cpp`
- Modify: `rk_app/src/shared/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Create: `rk_app/src/tests/shared_tests/app_runtime_config_test.cpp`

- [ ] **Step 1: Write the failing shared-config tests**

Create `rk_app/src/tests/shared_tests/app_runtime_config_test.cpp` with coverage for defaults, JSON load, env override precedence, path expansion, and validation:

```cpp
#include "runtime_config/app_runtime_config_loader.h"

#include <QTemporaryDir>
#include <QFile>
#include <QtTest/QTest>

class AppRuntimeConfigTest : public QObject {
    Q_OBJECT

private slots:
    void loadsBuiltInDefaults();
    void loadsJsonValuesFromFile();
    void environmentOverridesJsonValues();
    void resolvesRelativePathsAgainstConfigDirectory();
    void rejectsInvalidEnumValues();
};

void AppRuntimeConfigTest::loadsBuiltInDefaults() {
    const auto result = loadAppRuntimeConfig(QString());
    QVERIFY(result.ok);
    QCOMPARE(result.config.video.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(result.config.video.devicePath, QStringLiteral("/dev/video11"));
    QCOMPARE(result.config.analysis.transport, QStringLiteral("shared_memory"));
}

void AppRuntimeConfigTest::loadsJsonValuesFromFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "video": { "camera_id": "rear_cam", "device_path": "/dev/video22" },
        "analysis": { "transport": "dmabuf" }
    })");
    file.close();

    const auto result = loadAppRuntimeConfig(file.fileName());
    QVERIFY(result.ok);
    QCOMPARE(result.config.video.cameraId, QStringLiteral("rear_cam"));
    QCOMPARE(result.config.analysis.transport, QStringLiteral("dmabuf"));
}

void AppRuntimeConfigTest::environmentOverridesJsonValues() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({ "analysis": { "transport": "shared_memory" } })");
    file.close();

    qputenv("RK_VIDEO_ANALYSIS_TRANSPORT", QByteArray("dmabuf"));
    const auto result = loadAppRuntimeConfig(file.fileName());
    QCOMPARE(result.config.analysis.transport, QStringLiteral("dmabuf"));
    QCOMPARE(result.origins.value(QStringLiteral("analysis.transport")), QStringLiteral("environment"));
    qunsetenv("RK_VIDEO_ANALYSIS_TRANSPORT");
}
```

- [ ] **Step 2: Register the new test target and verify it fails**

Update `rk_app/src/tests/CMakeLists.txt`:

```cmake
add_executable(app_runtime_config_test
    shared_tests/app_runtime_config_test.cpp
    ../shared/runtime_config/app_runtime_config.cpp
    ../shared/runtime_config/app_runtime_config_loader.cpp
    ../shared/runtime_config/app_runtime_config_validator.cpp
    ../shared/runtime_config/app_runtime_config_paths.cpp
)

set_target_properties(app_runtime_config_test PROPERTIES
    AUTOMOC ON
)

target_include_directories(app_runtime_config_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(app_runtime_config_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME app_runtime_config_test COMMAND app_runtime_config_test)
```

Run:

```bash
bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON
ctest --test-dir out/build-rk_app-host -R app_runtime_config_test --output-on-failure
```

Expected: build or test fails because the new runtime-config files and symbols do not exist yet.

- [ ] **Step 3: Implement the typed config model, loader, validator, and path helpers**

Add `rk_app/src/shared/runtime_config/app_runtime_config.h` with one top-level config plus per-area structs:

```cpp
struct SystemRuntimeConfig {
    QString runtimeMode = QStringLiteral("auto");
};

struct PathsRuntimeConfig {
    QString storageDir = QStringLiteral("/home/elf/videosurv/");
    QString databasePath = QStringLiteral("./data/healthd.sqlite");
};

struct VideoRuntimeConfig {
    QString cameraId = QStringLiteral("front_cam");
    QString devicePath = QStringLiteral("/dev/video11");
    QString pipelineBackend = QStringLiteral("process");
    bool analysisEnabled = true;
    QString analysisConvertBackend = QStringLiteral("gstreamer_cpu");
    QString gstLaunchBin = QStringLiteral("gst-launch-1.0");
};

struct AppRuntimeConfig {
    SystemRuntimeConfig system;
    PathsRuntimeConfig paths;
    IpcRuntimeConfig ipc;
    VideoRuntimeConfig video;
    AnalysisRuntimeConfig analysis;
    FallDetectionRuntimeConfig fallDetection;
    DebugRuntimeConfig debug;
};

struct LoadedAppRuntimeConfig {
    bool ok = false;
    AppRuntimeConfig config;
    QStringList errors;
    QStringList warnings;
    QHash<QString, QString> origins;
    QString configPath;
};
```

In `app_runtime_config_loader.cpp`, implement:

```cpp
LoadedAppRuntimeConfig loadAppRuntimeConfig(const QString &explicitPath) {
    LoadedAppRuntimeConfig result;
    result.config = buildDefaultAppRuntimeConfig();
    result.configPath = resolveRuntimeConfigPath(explicitPath);

    QJsonParseError parseError;
    const QJsonDocument document = loadRuntimeConfigDocument(result.configPath, &parseError, &result.errors);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        applyRuntimeConfigJson(document.object(), &result.config, &result.origins);
    }

    applyEnvironmentOverrides(&result.config, &result.origins);
    normalizeRuntimeConfigPaths(result.configPath, &result.config);
    validateAppRuntimeConfig(result.config, &result.errors, &result.warnings);
    result.ok = result.errors.isEmpty();
    return result;
}
```

In `rk_app/src/shared/CMakeLists.txt`, add the new sources to `rk_shared`:

```cmake
add_library(rk_shared STATIC
    models/fall_models.cpp
    debug/latency_marker_writer.cpp
    protocol/device_frame.cpp
    protocol/ipc_message.cpp
    protocol/unix_fd_passing.cpp
    protocol/analysis_stream_protocol.cpp
    protocol/analysis_frame_descriptor_protocol.cpp
    protocol/fall_ipc.cpp
    protocol/video_ipc.cpp
    security/hmac_helper.cpp
    storage/database.cpp
    runtime_config/app_runtime_config.cpp
    runtime_config/app_runtime_config_loader.cpp
    runtime_config/app_runtime_config_validator.cpp
    runtime_config/app_runtime_config_paths.cpp
)
```

- [ ] **Step 4: Run the shared-config test and a small protocol regression set**

Run:

```bash
bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON
ctest --test-dir out/build-rk_app-host -R 'app_runtime_config_test|video_protocol_test|fall_protocol_test' --output-on-failure
```

Expected: all listed tests pass.

- [ ] **Step 5: Commit the shared-config core**

```bash
git add \
  rk_app/src/shared/CMakeLists.txt \
  rk_app/src/shared/runtime_config \
  rk_app/src/tests/CMakeLists.txt \
  rk_app/src/tests/shared_tests/app_runtime_config_test.cpp
git commit -m "feat: add shared runtime config loader"
```

### Task 2: Inject shared config into all runtime services and update daemon tests

**Files:**
- Modify: `rk_app/src/healthd/main.cpp`
- Modify: `rk_app/src/healthd/app/daemon_app.h`
- Modify: `rk_app/src/healthd/app/daemon_app.cpp`
- Modify: `rk_app/src/healthd/ipc_server/ui_gateway.cpp`
- Modify: `rk_app/src/health_videod/main.cpp`
- Modify: `rk_app/src/health_videod/app/video_daemon_app.h`
- Modify: `rk_app/src/health_videod/app/video_daemon_app.cpp`
- Modify: `rk_app/src/health_videod/core/video_service.h`
- Modify: `rk_app/src/health_videod/core/video_service.cpp`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.h`
- Modify: `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.cpp`
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h`
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`
- Modify: `rk_app/src/health_videod/analysis/rga_frame_converter.h`
- Modify: `rk_app/src/health_videod/analysis/rga_frame_converter.cpp`
- Modify: `rk_app/src/health_videod/ipc/video_gateway.cpp`
- Modify: `rk_app/src/health_falld/main.cpp`
- Modify: `rk_app/src/health_falld/runtime/runtime_config.h`
- Modify: `rk_app/src/health_falld/runtime/runtime_config.cpp`
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.h`
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.cpp`
- Modify: `rk_app/src/health_falld/ingest/analysis_stream_client.cpp`
- Modify: `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp`
- Modify: `rk_app/src/health_falld/action/rknn_lstm_action_classifier.cpp`
- Modify: `rk_app/src/health_ui/main.cpp`
- Modify: `rk_app/src/health_ui/app/ui_app.h`
- Modify: `rk_app/src/health_ui/app/ui_app.cpp`
- Modify: `rk_app/src/health_ui/ipc_client/ui_ipc_client.cpp`
- Modify: `rk_app/src/health_ui/ipc_client/video_ipc_client.cpp`
- Modify: `rk_app/src/health_ui/ipc_client/fall_ipc_client.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/action_classifier_factory_test.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/video_service_test.cpp`
- Modify: `rk_app/src/tests/ui_tests/health_ui_smoke_test.cpp`
- Create: `rk_app/src/tests/video_daemon_tests/video_runtime_config_integration_test.cpp`

- [ ] **Step 1: Write failing service-integration tests that use JSON config instead of direct env parsing**

Add `rk_app/src/tests/video_daemon_tests/video_runtime_config_integration_test.cpp`:

```cpp
#include "app/video_daemon_app.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

class VideoRuntimeConfigIntegrationTest : public QObject {
    Q_OBJECT

private slots:
    void loadsCameraAndSocketValuesFromJson();
};

void VideoRuntimeConfigIntegrationTest::loadsCameraAndSocketValuesFromJson() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "ipc": { "video_socket": "./run/video_custom.sock" },
        "video": {
            "camera_id": "rear_cam",
            "device_path": "/dev/video77",
            "analysis_enabled": false
        }
    })");
    file.close();

    qputenv("RK_APP_CONFIG_PATH", file.fileName().toUtf8());
    const auto loaded = loadAppRuntimeConfig(QString());
    QVERIFY(loaded.ok);
    QCOMPARE(loaded.config.video.cameraId, QStringLiteral("rear_cam"));
    QCOMPARE(loaded.config.video.devicePath, QStringLiteral("/dev/video77"));
    QCOMPARE(loaded.config.ipc.videoSocketPath.endsWith(QStringLiteral("video_custom.sock")), true);
    qunsetenv("RK_APP_CONFIG_PATH");
}
```

Also update existing tests so they construct or load config through the shared layer rather than relying on daemon code to call `qEnvironmentVariable(...)` directly.

- [ ] **Step 2: Register the new test and verify it fails before service wiring exists**

Extend `rk_app/src/tests/CMakeLists.txt`:

```cmake
add_executable(video_runtime_config_integration_test
    video_daemon_tests/video_runtime_config_integration_test.cpp
    ../health_videod/app/video_daemon_app.cpp
    ../health_videod/core/video_service.cpp
    ../health_videod/core/video_storage_service.cpp
    ../health_videod/ipc/video_gateway.cpp
)
```

Run:

```bash
bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON
ctest --test-dir out/build-rk_app-host -R 'video_runtime_config_integration_test|action_classifier_factory_test|health_ui_smoke_test' --output-on-failure
```

Expected: new test or existing runtime tests fail because the daemons are not yet config-driven.

- [ ] **Step 3: Inject typed config from `main.cpp` into each daemon and client**

In each `main.cpp`, load config once and stop boot if invalid:

```cpp
const LoadedAppRuntimeConfig loaded = loadAppRuntimeConfig(QString());
if (!loaded.ok) {
    for (const QString &error : loaded.errors) {
        qCritical().noquote() << "runtime_config error:" << error;
    }
    return 1;
}
```

Refactor constructors to accept service-specific config:

```cpp
class DaemonApp : public QObject {
public:
    explicit DaemonApp(const HealthdRuntimeConfig &config, QObject *parent = nullptr);
private:
    HealthdRuntimeConfig config_;
};

class VideoDaemonApp : public QObject {
public:
    explicit VideoDaemonApp(const VideoRuntimeConfig &config, QObject *parent = nullptr);
private:
    VideoRuntimeConfig config_;
};

class FallDaemonApp : public QObject {
public:
    explicit FallDaemonApp(
        const FallRuntimeConfig &config,
        std::unique_ptr<PoseEstimator> poseEstimator = std::make_unique<RknnPoseEstimator>(),
        QObject *parent = nullptr);
};
```

Replace direct env reads with config fields in:

```cpp
LatencyMarkerWriter marker(config_.videoLatencyMarkerPath);
gateway_->setSocketName(config_.videoSocketPath);
const QString socketName = config_.healthSocketPath;
const QString analysisSocketPath = config_.analysisSocketPath;
```

Keep `loadFallRuntimeConfig()` as a thin mapper:

```cpp
FallRuntimeConfig loadFallRuntimeConfig(const AppRuntimeConfig &appConfig) {
    FallRuntimeConfig config;
    config.cameraId = appConfig.video.cameraId;
    config.analysisSocketPath = appConfig.ipc.analysisSocketPath;
    config.poseModelPath = appConfig.fallDetection.poseModelPath;
    return config;
}
```

- [ ] **Step 4: Run focused daemon and UI tests**

Run:

```bash
bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON
ctest --test-dir out/build-rk_app-host -R 'app_runtime_config_test|video_runtime_config_integration_test|gstreamer_video_pipeline_backend_test|video_service_test|action_classifier_factory_test|health_ui_smoke_test' --output-on-failure
```

Expected: all listed tests pass, proving the shared config layer now drives the daemons and UI clients.

- [ ] **Step 5: Commit the service wiring**

```bash
git add \
  rk_app/src/healthd \
  rk_app/src/health_videod \
  rk_app/src/health_falld \
  rk_app/src/health_ui \
  rk_app/src/tests/fall_daemon_tests/action_classifier_factory_test.cpp \
  rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp \
  rk_app/src/tests/video_daemon_tests/video_service_test.cpp \
  rk_app/src/tests/video_daemon_tests/video_runtime_config_integration_test.cpp \
  rk_app/src/tests/ui_tests/health_ui_smoke_test.cpp
git commit -m "refactor: load daemon runtime from shared config"
```

### Task 3: Add the standalone `health-config-ui` and its UI tests

**Files:**
- Create: `rk_app/src/health_config_ui/CMakeLists.txt`
- Create: `rk_app/src/health_config_ui/main.cpp`
- Create: `rk_app/src/health_config_ui/app/config_editor_app.h`
- Create: `rk_app/src/health_config_ui/app/config_editor_app.cpp`
- Create: `rk_app/src/health_config_ui/widgets/config_editor_window.h`
- Create: `rk_app/src/health_config_ui/widgets/config_editor_window.cpp`
- Create: `rk_app/src/health_config_ui/widgets/config_section_widget.h`
- Create: `rk_app/src/health_config_ui/widgets/config_section_widget.cpp`
- Modify: `rk_app/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Create: `rk_app/src/tests/ui_tests/health_config_ui_test.cpp`

- [ ] **Step 1: Write the failing config-UI test**

Create `rk_app/src/tests/ui_tests/health_config_ui_test.cpp`:

```cpp
#include "widgets/config_editor_window.h"

#include <QTemporaryDir>
#include <QFile>
#include <QtTest/QTest>

class HealthConfigUiTest : public QObject {
    Q_OBJECT

private slots:
    void loadsJsonIntoWidgets();
    void marksDirtyAfterEditAndSaves();
};

void HealthConfigUiTest::loadsJsonIntoWidgets() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "video": { "device_path": "/dev/video55" },
        "analysis": { "transport": "dmabuf" }
    })");
    file.close();

    ConfigEditorWindow window(file.fileName());
    QVERIFY(window.load());
    QCOMPARE(window.valueForField(QStringLiteral("video.device_path")), QStringLiteral("/dev/video55"));
    QCOMPARE(window.valueForField(QStringLiteral("analysis.transport")), QStringLiteral("dmabuf"));
}
```

- [ ] **Step 2: Register the new executable and test target, then verify failure**

In `rk_app/CMakeLists.txt`, add:

```cmake
add_subdirectory(src/health_config_ui)
```

In `rk_app/src/tests/CMakeLists.txt`, add:

```cmake
add_executable(health_config_ui_test
    ui_tests/health_config_ui_test.cpp
    ../health_config_ui/app/config_editor_app.cpp
    ../health_config_ui/widgets/config_editor_window.cpp
    ../health_config_ui/widgets/config_section_widget.cpp
)
```

Run:

```bash
bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON
ctest --test-dir out/build-rk_app-host -R health_config_ui_test --output-on-failure
```

Expected: build or test fails because the new UI target and classes do not exist yet.

- [ ] **Step 3: Implement the standalone configuration editor**

Create `rk_app/src/health_config_ui/CMakeLists.txt`:

```cmake
if(RK_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    set(RK_CONFIG_UI_WIDGETS_TARGET Qt6::Widgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(RK_CONFIG_UI_WIDGETS_TARGET Qt5::Widgets)
endif()

add_executable(health-config-ui
    main.cpp
    app/config_editor_app.cpp
    app/config_editor_app.h
    widgets/config_editor_window.cpp
    widgets/config_editor_window.h
    widgets/config_section_widget.cpp
    widgets/config_section_widget.h
)

set_target_properties(health-config-ui PROPERTIES AUTOMOC ON)

target_include_directories(health-config-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(health-config-ui PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_CONFIG_UI_WIDGETS_TARGET}
)
```

Implement `ConfigEditorWindow` with grouped sections, validate/save/reload actions, and a minimal field API used by the test:

```cpp
class ConfigEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ConfigEditorWindow(const QString &configPath, QWidget *parent = nullptr);
    bool load();
    QString valueForField(const QString &fieldPath) const;
public slots:
    bool save();
    void restoreDefaults();
private:
    QString configPath_;
    AppRuntimeConfig currentConfig_;
    QLabel *statusLabel_ = nullptr;
    QHash<QString, QWidget *> fieldWidgets_;
};
```

- [ ] **Step 4: Run the new config-UI test and existing UI smoke tests**

Run:

```bash
bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON
ctest --test-dir out/build-rk_app-host -R 'health_config_ui_test|health_ui_smoke_test|video_preview_widget_test' --output-on-failure
```

Expected: all listed UI tests pass.

- [ ] **Step 5: Commit the standalone UI**

```bash
git add \
  rk_app/CMakeLists.txt \
  rk_app/src/health_config_ui \
  rk_app/src/tests/CMakeLists.txt \
  rk_app/src/tests/ui_tests/health_config_ui_test.cpp
git commit -m "feat: add standalone runtime config ui"
```

### Task 4: Package JSON config and simplify bundle startup scripts

**Files:**
- Create: `deploy/config/runtime_config.json`
- Create: `deploy/bundle/config.sh`
- Modify: `deploy/bundle/README.txt`
- Modify: `deploy/bundle/start.sh`
- Modify: `deploy/bundle/start_all.sh`
- Modify: `deploy/scripts/build_rk3588_bundle.sh`
- Modify: `deploy/tests/start_runtime_mode_test.sh`
- Modify: `deploy/tests/start_all_display_detection_test.sh`
- Create: `deploy/tests/runtime_config_bundle_test.sh`

- [ ] **Step 1: Write a failing bundle-config shell test**

Create `deploy/tests/runtime_config_bundle_test.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUNDLE_DIR=$(mktemp -d)
trap 'rm -rf "${BUNDLE_DIR}"' EXIT

mkdir -p "${BUNDLE_DIR}/config" "${BUNDLE_DIR}/scripts" "${BUNDLE_DIR}/bin"
cp "${ROOT}/deploy/bundle/start.sh" "${BUNDLE_DIR}/scripts/start.sh"
cp "${ROOT}/deploy/config/runtime_config.json" "${BUNDLE_DIR}/config/runtime_config.json"

grep -q 'RK_APP_CONFIG_PATH' "${BUNDLE_DIR}/scripts/start.sh"
grep -q '"video"' "${BUNDLE_DIR}/config/runtime_config.json"
```

Add execute permission in the same commit:

```bash
chmod +x deploy/tests/runtime_config_bundle_test.sh
```

- [ ] **Step 2: Run the new script test and confirm failure before packaging changes**

Run:

```bash
bash deploy/tests/runtime_config_bundle_test.sh
```

Expected: the script fails because `deploy/config/runtime_config.json` and the updated `start.sh` behavior do not exist yet.

- [ ] **Step 3: Add bundle config assets and update startup scripts**

Create `deploy/config/runtime_config.json` with stable defaults:

```json
{
  "system": {
    "runtime_mode": "auto"
  },
  "paths": {
    "storage_dir": "/home/elf/videosurv/",
    "database_path": "./data/healthd.sqlite"
  },
  "ipc": {
    "health_socket": "./run/rk_health_station.sock",
    "video_socket": "./run/rk_video.sock",
    "analysis_socket": "./run/rk_video_analysis.sock",
    "fall_socket": "./run/rk_fall.sock"
  },
  "video": {
    "camera_id": "front_cam",
    "device_path": "/dev/video11",
    "pipeline_backend": "process",
    "analysis_enabled": true,
    "analysis_convert_backend": "gstreamer_cpu",
    "gst_launch_bin": "gst-launch-1.0"
  }
}
```

Refactor `deploy/bundle/start.sh` so it sets only `RK_APP_CONFIG_PATH` plus runtime-library variables:

```bash
CONFIG_PATH="${RK_APP_CONFIG_PATH:-${BUNDLE_ROOT}/config/runtime_config.json}"

if [[ ! -f "${CONFIG_PATH}" ]]; then
  echo "missing runtime config: ${CONFIG_PATH}" >&2
  exit 1
fi

export RK_APP_CONFIG_PATH="${CONFIG_PATH}"
echo "runtime config: ${RK_APP_CONFIG_PATH}"
```

Add `deploy/bundle/config.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUNDLE_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
export RK_APP_CONFIG_PATH="${RK_APP_CONFIG_PATH:-${BUNDLE_ROOT}/config/runtime_config.json}"

exec "${BUNDLE_ROOT}/bin/health-config-ui"
```

Update `deploy/scripts/build_rk3588_bundle.sh` to package:

```bash
mkdir -p "${BUNDLE_DIR}/config"
install -m 644 "${PROJECT_ROOT}/deploy/config/runtime_config.json" \
  "${BUNDLE_DIR}/config/runtime_config.json"
install -m 755 "${PROJECT_ROOT}/deploy/bundle/config.sh" \
  "${BUNDLE_DIR}/scripts/config.sh"
install -m 755 "${BUILD_DIR}/src/health_config_ui/health-config-ui" \
  "${BUNDLE_DIR}/bin/health-config-ui"
```

- [ ] **Step 4: Run bundle script tests and host build**

Run:

```bash
bash deploy/tests/runtime_config_bundle_test.sh
bash deploy/tests/start_runtime_mode_test.sh
bash deploy/tests/start_all_display_detection_test.sh
bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON
```

Expected: the shell tests pass and the host build still succeeds.

- [ ] **Step 5: Commit the bundle/runtime-script changes**

```bash
git add \
  deploy/config/runtime_config.json \
  deploy/bundle/config.sh \
  deploy/bundle/README.txt \
  deploy/bundle/start.sh \
  deploy/bundle/start_all.sh \
  deploy/scripts/build_rk3588_bundle.sh \
  deploy/tests/start_runtime_mode_test.sh \
  deploy/tests/start_all_display_detection_test.sh \
  deploy/tests/runtime_config_bundle_test.sh
git commit -m "feat: ship bundle runtime config and config ui launcher"
```

### Task 5: Update docs and run full verification, including board smoke tests

**Files:**
- Modify: `README.md`
- Modify: `deploy/README.md`
- Modify: `docs/deployment/rk3588-install.md`
- Modify: `docs/DevelopmentLog/2026-04-28-dma-inprocess-gstreamer-development-log.md`

- [ ] **Step 1: Write down the user-facing documentation changes before editing**

Add documentation text that replaces long env-export examples with:

```md
1. Edit `config/runtime_config.json`, or run `./scripts/config.sh`
2. Start the bundle with `./scripts/start.sh` or `./scripts/start_all.sh`
3. Use environment variables only for temporary advanced overrides
```

Also add one explicit example of env override precedence:

```bash
RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf ./scripts/start.sh --backend-only
```

- [ ] **Step 2: Update the docs and usage snippets**

In `README.md`, replace the current primary user path of:

```bash
export RK_RUNTIME_MODE=system
export RK_VIDEO_PIPELINE_BACKEND=inproc_gst
export RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf
./scripts/start.sh
```

with:

```bash
./scripts/config.sh
./scripts/start.sh
```

and move the old env-based flow under an "Advanced Overrides" section.

- [ ] **Step 3: Run the final host and bundle verification set**

Run:

```bash
bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON
ctest --test-dir out/build-rk_app-host --output-on-failure
BUILD_DIR=/tmp/rk_health_station-build-rk3588-runtime-config \
  bash deploy/scripts/build_rk3588_bundle.sh
```

Expected: host build and full host test suite pass; RK3588 bundle build succeeds and includes `bin/health-config-ui`, `scripts/config.sh`, and `config/runtime_config.json`.

- [ ] **Step 4: Run board smoke tests for both compatibility and zero-copy configs**

Deploy the bundle, then verify:

```bash
ssh elf@192.168.137.179
cd /home/elf/rk3588_bundle
./scripts/config.sh
./scripts/start.sh --backend-only
printf '%s\n' '{"action":"start_preview","request_id":"json-config","camera_id":"front_cam","payload":{}}' | \
  nc -q 1 -U /home/elf/rk3588_bundle/run/rk_video.sock
```

Then edit `config/runtime_config.json` for a zero-copy-oriented case and rerun:

```json
"video": {
  "pipeline_backend": "inproc_gst",
  "analysis_convert_backend": "rga"
},
"analysis": {
  "transport": "dmabuf",
  "rga_output_dmabuf": true,
  "gst_dmabuf_input": true,
  "gst_force_dmabuf_io": true
},
"fall_detection": {
  "rknn_input_dmabuf": true
}
```

Expected: both startup modes work; logs show JSON-driven settings; advanced env overrides still work when explicitly supplied.

- [ ] **Step 5: Commit docs and verification notes**

```bash
git add \
  README.md \
  deploy/README.md \
  docs/deployment/rk3588-install.md \
  docs/DevelopmentLog/2026-04-28-dma-inprocess-gstreamer-development-log.md
git commit -m "docs: describe unified runtime config workflow"
```

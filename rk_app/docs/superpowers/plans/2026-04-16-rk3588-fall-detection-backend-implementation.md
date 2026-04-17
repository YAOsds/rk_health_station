# RK3588 Fall Detection Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a backend-only fall-detection pipeline to `rk_app` by extending `health-videod` with an optional analysis output path and adding a new `health-falld` daemon for pose inference, temporal classification, and event publication, without regressing the existing preview/record/snapshot chain.

**Architecture:** Keep `health-videod` as the sole camera owner and add a side-channel analysis output that is independent of the current preview transport. Add a new `health-falld` daemon that consumes that stream, runs `yolov8-pose` on RKNN/NPU, keeps temporal state for one primary subject, runs ST-GCN on CPU, and exposes AI status/events over local JSON IPC. All AI state and protocol live outside `health-videod` so failures stay isolated.

**Tech Stack:** C++17, Qt Core/Network/Test, existing `rk_shared` static library, `health-videod`, new `health-falld`, Unix domain sockets for frame transport, `QLocalSocket` JSON line IPC for status/events, RKNN Runtime for pose, CPU ST-GCN runtime, CMake/CTest.

---

## File Structure Map

### Existing files to modify

- `CMakeLists.txt`
  - add `src/health_falld`
- `src/shared/CMakeLists.txt`
  - compile new shared models/protocol files into `rk_shared`
- `src/health_videod/CMakeLists.txt`
  - compile new analysis output files into `health-videod`
- `src/health_videod/app/video_daemon_app.cpp`
  - wire analysis output startup behind config
- `src/health_videod/core/video_service.h`
  - add analysis status/config plumbing without leaking AI types
- `src/health_videod/core/video_service.cpp`
  - manage optional analysis output lifecycle
- `src/tests/CMakeLists.txt`
  - add new protocol, video-daemon, and fall-daemon test targets

### New shared files

- `src/shared/models/fall_models.h`
- `src/shared/protocol/fall_ipc.h`
- `src/shared/protocol/fall_ipc.cpp`
- `src/shared/protocol/analysis_stream_protocol.h`
- `src/shared/protocol/analysis_stream_protocol.cpp`

### New video-daemon analysis files

- `src/health_videod/analysis/analysis_output_backend.h`
- `src/health_videod/analysis/gstreamer_analysis_output_backend.h`
- `src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`

### New fall-daemon files

- `src/health_falld/CMakeLists.txt`
- `src/health_falld/main.cpp`
- `src/health_falld/app/fall_daemon_app.h`
- `src/health_falld/app/fall_daemon_app.cpp`
- `src/health_falld/ingest/analysis_stream_client.h`
- `src/health_falld/ingest/analysis_stream_client.cpp`
- `src/health_falld/pose/pose_types.h`
- `src/health_falld/pose/pose_estimator.h`
- `src/health_falld/pose/rknn_pose_estimator.h`
- `src/health_falld/pose/rknn_pose_estimator.cpp`
- `src/health_falld/action/action_classifier.h`
- `src/health_falld/action/stgcn_action_classifier.h`
- `src/health_falld/action/stgcn_action_classifier.cpp`
- `src/health_falld/action/sequence_buffer.h`
- `src/health_falld/action/sequence_buffer.cpp`
- `src/health_falld/action/target_selector.h`
- `src/health_falld/action/target_selector.cpp`
- `src/health_falld/domain/fall_detector_service.h`
- `src/health_falld/domain/fall_detector_service.cpp`
- `src/health_falld/domain/fall_event_policy.h`
- `src/health_falld/domain/fall_event_policy.cpp`
- `src/health_falld/ipc/fall_gateway.h`
- `src/health_falld/ipc/fall_gateway.cpp`
- `src/health_falld/runtime/runtime_config.h`
- `src/health_falld/runtime/runtime_config.cpp`

### New tests

- `src/tests/protocol_tests/analysis_stream_protocol_test.cpp`
- `src/tests/protocol_tests/fall_protocol_test.cpp`
- `src/tests/video_daemon_tests/analysis_output_backend_test.cpp`
- `src/tests/video_daemon_tests/video_service_analysis_test.cpp`
- `src/tests/fall_daemon_tests/sequence_buffer_test.cpp`
- `src/tests/fall_daemon_tests/fall_event_policy_test.cpp`
- `src/tests/fall_daemon_tests/analysis_stream_client_test.cpp`
- `src/tests/fall_daemon_tests/fall_gateway_test.cpp`

### Assets and config placeholders to add later in implementation

- `assets/models/yolov8n-pose.rknn`
- `assets/models/stgcn_fall.onnx`
- `config/fall_detection.yaml`

## Notes Before Implementation

- This tree currently has no `.git` metadata, so the "Commit" steps below are written as checkpoint commands to use if the project is later placed under git or copied into a git worktree.
- Keep `video.analysis.enabled=false` by default throughout the implementation until board validation passes.
- Do not import training scripts from `yolo_detect` directly into runtime code. Re-implement only the minimal inference-time logic.

### Task 1: Extend Shared Models and Protocols

**Files:**
- Create: `src/shared/models/fall_models.h`
- Create: `src/shared/protocol/fall_ipc.h`
- Create: `src/shared/protocol/fall_ipc.cpp`
- Create: `src/shared/protocol/analysis_stream_protocol.h`
- Create: `src/shared/protocol/analysis_stream_protocol.cpp`
- Modify: `src/shared/CMakeLists.txt`
- Test: `src/tests/protocol_tests/analysis_stream_protocol_test.cpp`
- Test: `src/tests/protocol_tests/fall_protocol_test.cpp`
- Modify: `src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing protocol tests**

```cpp
// src/tests/protocol_tests/analysis_stream_protocol_test.cpp
#include "protocol/analysis_stream_protocol.h"

#include <QtTest/QTest>

class AnalysisStreamProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsFramePacket();
};

void AnalysisStreamProtocolTest::roundTripsFramePacket() {
    AnalysisFramePacket packet;
    packet.frameId = 7;
    packet.timestampMs = 123456;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 640;
    packet.pixelFormat = AnalysisPixelFormat::Jpeg;
    packet.payload = QByteArray::fromHex("FFD8FFD9");

    QByteArray encoded = encodeAnalysisFramePacket(packet);
    AnalysisFramePacket decoded;
    QVERIFY(decodeAnalysisFramePacket(encoded, &decoded));
    QCOMPARE(decoded.frameId, packet.frameId);
    QCOMPARE(decoded.cameraId, packet.cameraId);
    QCOMPARE(decoded.pixelFormat, packet.pixelFormat);
    QCOMPARE(decoded.payload, packet.payload);
}

QTEST_MAIN(AnalysisStreamProtocolTest)
#include "analysis_stream_protocol_test.moc"
```

```cpp
// src/tests/protocol_tests/fall_protocol_test.cpp
#include "protocol/fall_ipc.h"

#include <QtTest/QTest>

class FallProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsRuntimeStatusJson();
};

void FallProtocolTest::roundTripsRuntimeStatusJson() {
    FallRuntimeStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.inputConnected = true;
    status.poseModelReady = true;
    status.actionModelReady = false;
    status.currentFps = 9.5;
    status.latestState = QStringLiteral("monitoring");
    status.latestConfidence = 0.75;

    const QJsonObject json = fallRuntimeStatusToJson(status);
    FallRuntimeStatus decoded;
    QVERIFY(fallRuntimeStatusFromJson(json, &decoded));
    QCOMPARE(decoded.cameraId, status.cameraId);
    QCOMPARE(decoded.inputConnected, status.inputConnected);
    QCOMPARE(decoded.latestState, status.latestState);
}

QTEST_MAIN(FallProtocolTest)
#include "fall_protocol_test.moc"
```

- [ ] **Step 2: Register the new test targets and run them to verify they fail**

```cmake
# src/tests/CMakeLists.txt
add_executable(analysis_stream_protocol_test
    protocol_tests/analysis_stream_protocol_test.cpp
)
set_target_properties(analysis_stream_protocol_test PROPERTIES AUTOMOC ON)
target_link_libraries(analysis_stream_protocol_test PRIVATE rk_shared ${RK_QT_TEST_TARGET})
add_test(NAME analysis_stream_protocol_test COMMAND analysis_stream_protocol_test)

add_executable(fall_protocol_test
    protocol_tests/fall_protocol_test.cpp
)
set_target_properties(fall_protocol_test PROPERTIES AUTOMOC ON)
target_link_libraries(fall_protocol_test PRIVATE rk_shared ${RK_QT_TEST_TARGET})
add_test(NAME fall_protocol_test COMMAND fall_protocol_test)
```

Run:

```bash
cmake -S /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/rk_app -B /tmp/rk_health_station-build
cmake --build /tmp/rk_health_station-build --target analysis_stream_protocol_test fall_protocol_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'analysis_stream_protocol_test|fall_protocol_test' --output-on-failure
```

Expected: build fails because `analysis_stream_protocol.h` and `fall_ipc.h` do not exist yet.

- [ ] **Step 3: Add the shared model and protocol implementation**

```cpp
// src/shared/models/fall_models.h
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

enum class AnalysisPixelFormat {
    Jpeg,
    Nv12,
};

struct AnalysisFramePacket {
    quint64 frameId = 0;
    qint64 timestampMs = 0;
    QString cameraId;
    qint32 width = 0;
    qint32 height = 0;
    AnalysisPixelFormat pixelFormat = AnalysisPixelFormat::Jpeg;
    QByteArray payload;
};

struct AnalysisChannelStatus {
    QString cameraId;
    bool enabled = false;
    bool streamConnected = false;
    QString outputFormat;
    int width = 0;
    int height = 0;
    int fps = 0;
    quint64 droppedFrames = 0;
    QString lastError;
};

struct FallRuntimeStatus {
    QString cameraId;
    bool inputConnected = false;
    bool poseModelReady = false;
    bool actionModelReady = false;
    double currentFps = 0.0;
    qint64 lastFrameTs = 0;
    qint64 lastInferTs = 0;
    QString latestState;
    double latestConfidence = 0.0;
    QString lastError;
};

struct FallEvent {
    QString eventId;
    QString cameraId;
    qint64 tsStart = 0;
    qint64 tsConfirm = 0;
    QString eventType;
    double confidence = 0.0;
    QString snapshotRef;
    QString clipRef;
};
```

```cpp
// src/shared/protocol/analysis_stream_protocol.h
#pragma once

#include "models/fall_models.h"

QByteArray encodeAnalysisFramePacket(const AnalysisFramePacket &packet);
bool decodeAnalysisFramePacket(const QByteArray &bytes, AnalysisFramePacket *packet);
```

```cpp
// src/shared/protocol/fall_ipc.h
#pragma once

#include "models/fall_models.h"

#include <QJsonObject>

QJsonObject fallRuntimeStatusToJson(const FallRuntimeStatus &status);
bool fallRuntimeStatusFromJson(const QJsonObject &json, FallRuntimeStatus *status);
QJsonObject fallEventToJson(const FallEvent &event);
bool fallEventFromJson(const QJsonObject &json, FallEvent *event);
```

```cmake
# src/shared/CMakeLists.txt
add_library(rk_shared STATIC
    protocol/device_frame.cpp
    protocol/ipc_message.cpp
    protocol/video_ipc.cpp
    protocol/fall_ipc.cpp
    protocol/analysis_stream_protocol.cpp
    security/hmac_helper.cpp
    storage/database.cpp
)
```

- [ ] **Step 4: Run the protocol tests to verify they pass**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target analysis_stream_protocol_test fall_protocol_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'analysis_stream_protocol_test|fall_protocol_test' --output-on-failure
```

Expected: both tests PASS.

- [ ] **Step 5: Checkpoint**

If this tree is under git in the execution workspace:

```bash
git add src/shared/CMakeLists.txt \
  src/shared/models/fall_models.h \
  src/shared/protocol/fall_ipc.h \
  src/shared/protocol/fall_ipc.cpp \
  src/shared/protocol/analysis_stream_protocol.h \
  src/shared/protocol/analysis_stream_protocol.cpp \
  src/tests/CMakeLists.txt \
  src/tests/protocol_tests/analysis_stream_protocol_test.cpp \
  src/tests/protocol_tests/fall_protocol_test.cpp
git commit -m "feat: add fall detection shared protocols"
```

### Task 2: Add Optional Analysis Output to `health-videod`

**Files:**
- Create: `src/health_videod/analysis/analysis_output_backend.h`
- Create: `src/health_videod/analysis/gstreamer_analysis_output_backend.h`
- Create: `src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`
- Modify: `src/health_videod/core/video_service.h`
- Modify: `src/health_videod/core/video_service.cpp`
- Modify: `src/health_videod/app/video_daemon_app.cpp`
- Modify: `src/health_videod/CMakeLists.txt`
- Test: `src/tests/video_daemon_tests/analysis_output_backend_test.cpp`
- Test: `src/tests/video_daemon_tests/video_service_analysis_test.cpp`
- Modify: `src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing analysis-output tests**

```cpp
// src/tests/video_daemon_tests/video_service_analysis_test.cpp
#include "core/video_service.h"

#include <QtTest/QTest>

class FakeAnalysisOutputBackend : public AnalysisOutputBackend {
public:
    bool start(const VideoChannelStatus &status, QString *error) override {
        Q_UNUSED(status);
        if (error) {
            error->clear();
        }
        started = true;
        return true;
    }

    bool stop(const QString &cameraId, QString *error) override {
        Q_UNUSED(cameraId);
        if (error) {
            error->clear();
        }
        stopped = true;
        return true;
    }

    AnalysisChannelStatus statusForCamera(const QString &cameraId) const override {
        AnalysisChannelStatus status;
        status.cameraId = cameraId;
        status.enabled = started && !stopped;
        status.streamConnected = started && !stopped;
        status.outputFormat = QStringLiteral("jpeg");
        status.width = 640;
        status.height = 640;
        status.fps = 10;
        return status;
    }

    mutable bool started = false;
    mutable bool stopped = false;
};

class VideoServiceAnalysisTest : public QObject {
    Q_OBJECT

private slots:
    void analysisIsDisabledByDefault();
};

void VideoServiceAnalysisTest::analysisIsDisabledByDefault() {
    FakeAnalysisOutputBackend analysisBackend;
    VideoService service(nullptr, &analysisBackend, nullptr);

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.cameraState, VideoCameraState::Idle);
    QVERIFY(!analysisBackend.started);
}

QTEST_MAIN(VideoServiceAnalysisTest)
#include "video_service_analysis_test.moc"
```

```cpp
// src/tests/video_daemon_tests/analysis_output_backend_test.cpp
#include "analysis/gstreamer_analysis_output_backend.h"

#include <QtTest/QTest>

class AnalysisOutputBackendTest : public QObject {
    Q_OBJECT

private slots:
    void resolvesAnalysisSocketFromEnvironment();
};

void AnalysisOutputBackendTest::resolvesAnalysisSocketFromEnvironment() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", "/tmp/rk_video_analysis.sock");
    GstreamerAnalysisOutputBackend backend;
    QCOMPARE(backend.socketPath(), QStringLiteral("/tmp/rk_video_analysis.sock"));
}

QTEST_MAIN(AnalysisOutputBackendTest)
#include "analysis_output_backend_test.moc"
```

- [ ] **Step 2: Register the tests and run them to verify they fail**

```cmake
# src/tests/CMakeLists.txt
add_executable(analysis_output_backend_test
    video_daemon_tests/analysis_output_backend_test.cpp
    ../health_videod/analysis/gstreamer_analysis_output_backend.cpp
)
set_target_properties(analysis_output_backend_test PROPERTIES AUTOMOC ON)
target_include_directories(analysis_output_backend_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_videod
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)
target_link_libraries(analysis_output_backend_test PRIVATE rk_shared ${RK_QT_TEST_TARGET})
add_test(NAME analysis_output_backend_test COMMAND analysis_output_backend_test)
```

Run:

```bash
cmake --build /tmp/rk_health_station-build --target analysis_output_backend_test video_service_analysis_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'analysis_output_backend_test|video_service_analysis_test' --output-on-failure
```

Expected: build fails because `AnalysisOutputBackend` and `GstreamerAnalysisOutputBackend` are not defined yet.

- [ ] **Step 3: Add the analysis output abstraction and wire it into `VideoService` behind a disabled-by-default config**

```cpp
// src/health_videod/analysis/analysis_output_backend.h
#pragma once

#include "models/fall_models.h"
#include "models/video_models.h"

class AnalysisOutputBackend {
public:
    virtual ~AnalysisOutputBackend() = default;
    virtual bool start(const VideoChannelStatus &status, QString *error) = 0;
    virtual bool stop(const QString &cameraId, QString *error) = 0;
    virtual AnalysisChannelStatus statusForCamera(const QString &cameraId) const = 0;
};
```

```cpp
// src/health_videod/core/video_service.h
class AnalysisOutputBackend;

class VideoService : public QObject {
    Q_OBJECT
public:
    explicit VideoService(
        VideoPipelineBackend *pipelineBackend = nullptr,
        AnalysisOutputBackend *analysisBackend = nullptr,
        QObject *parent = nullptr);
    AnalysisChannelStatus analysisStatusForCamera(const QString &cameraId) const;

private:
    bool analysisEnabledForCamera(const QString &cameraId) const;
    void syncAnalysisOutput(const QString &cameraId);

    VideoPipelineBackend *pipelineBackend_ = nullptr;
    AnalysisOutputBackend *analysisBackend_ = nullptr;
    bool ownsPipelineBackend_ = false;
    bool ownsAnalysisBackend_ = false;
};
```

```cpp
// src/health_videod/app/video_daemon_app.cpp
bool VideoDaemonApp::start() {
    if (!gateway_->start()) {
        return false;
    }
    service_->startPreview(QStringLiteral("front_cam"));
    service_->analysisStatusForCamera(QStringLiteral("front_cam"));
    return true;
}
```

```cmake
# src/health_videod/CMakeLists.txt
add_executable(health-videod
    main.cpp
    app/video_daemon_app.cpp
    app/video_daemon_app.h
    core/video_service.cpp
    core/video_service.h
    core/video_storage_service.cpp
    core/video_storage_service.h
    analysis/gstreamer_analysis_output_backend.cpp
    analysis/gstreamer_analysis_output_backend.h
    analysis/analysis_output_backend.h
    pipeline/gstreamer_video_pipeline_backend.cpp
    pipeline/gstreamer_video_pipeline_backend.h
    pipeline/video_pipeline_backend.h
    ipc/video_gateway.cpp
    ipc/video_gateway.h
)
```

- [ ] **Step 4: Implement the minimal backend and update tests to assert no regression**

```cpp
// src/health_videod/analysis/gstreamer_analysis_output_backend.h
#pragma once

#include "analysis/analysis_output_backend.h"

class GstreamerAnalysisOutputBackend : public AnalysisOutputBackend {
public:
    QString socketPath() const;
    bool start(const VideoChannelStatus &status, QString *error) override;
    bool stop(const QString &cameraId, QString *error) override;
    AnalysisChannelStatus statusForCamera(const QString &cameraId) const override;

private:
    QHash<QString, AnalysisChannelStatus> statuses_;
};
```

```cpp
// src/health_videod/analysis/gstreamer_analysis_output_backend.cpp
QString GstreamerAnalysisOutputBackend::socketPath() const {
    const QString path = qEnvironmentVariable("RK_VIDEO_ANALYSIS_SOCKET_PATH");
    return path.isEmpty() ? QStringLiteral("/tmp/rk_video_analysis.sock") : path;
}

bool GstreamerAnalysisOutputBackend::start(const VideoChannelStatus &status, QString *error) {
    if (error) {
        error->clear();
    }
    AnalysisChannelStatus analysis;
    analysis.cameraId = status.cameraId;
    analysis.enabled = qEnvironmentVariableIntValue("RK_VIDEO_ANALYSIS_ENABLED") == 1;
    analysis.streamConnected = false;
    analysis.outputFormat = QStringLiteral("jpeg");
    analysis.width = 640;
    analysis.height = 640;
    analysis.fps = 10;
    statuses_.insert(status.cameraId, analysis);
    return true;
}
```

- [ ] **Step 5: Run the analysis-output tests and a targeted original-chain regression set**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target \
  analysis_output_backend_test \
  video_service_analysis_test \
  video_service_test \
  video_gateway_test \
  video_monitor_page_test -j4

ctest --test-dir /tmp/rk_health_station-build -R \
  'analysis_output_backend_test|video_service_analysis_test|video_service_test|video_gateway_test|video_monitor_page_test' \
  --output-on-failure
```

Expected: new tests PASS, existing targeted video tests still PASS.

- [ ] **Step 6: Checkpoint**

```bash
git add src/health_videod/CMakeLists.txt \
  src/health_videod/app/video_daemon_app.cpp \
  src/health_videod/core/video_service.h \
  src/health_videod/core/video_service.cpp \
  src/health_videod/analysis/analysis_output_backend.h \
  src/health_videod/analysis/gstreamer_analysis_output_backend.h \
  src/health_videod/analysis/gstreamer_analysis_output_backend.cpp \
  src/tests/CMakeLists.txt \
  src/tests/video_daemon_tests/analysis_output_backend_test.cpp \
  src/tests/video_daemon_tests/video_service_analysis_test.cpp
git commit -m "feat: add optional video analysis output"
```

### Task 3: Create the `health-falld` Skeleton Service and Runtime Status IPC

**Files:**
- Create: `src/health_falld/CMakeLists.txt`
- Create: `src/health_falld/main.cpp`
- Create: `src/health_falld/app/fall_daemon_app.h`
- Create: `src/health_falld/app/fall_daemon_app.cpp`
- Create: `src/health_falld/ipc/fall_gateway.h`
- Create: `src/health_falld/ipc/fall_gateway.cpp`
- Create: `src/health_falld/runtime/runtime_config.h`
- Create: `src/health_falld/runtime/runtime_config.cpp`
- Modify: `CMakeLists.txt`
- Test: `src/tests/fall_daemon_tests/fall_gateway_test.cpp`
- Modify: `src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing `health-falld` IPC test**

```cpp
// src/tests/fall_daemon_tests/fall_gateway_test.cpp
#include "ipc/fall_gateway.h"

#include <QLocalSocket>
#include <QtTest/QTest>

class FallGatewayTest : public QObject {
    Q_OBJECT

private slots:
    void returnsRuntimeStatus();
};

void FallGatewayTest::returnsRuntimeStatus() {
    FallRuntimeStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.inputConnected = true;
    status.poseModelReady = false;
    status.actionModelReady = false;
    status.latestState = QStringLiteral("monitoring");

    FallGateway gateway(status);
    QVERIFY(gateway.start());

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("rk_fall.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QVERIFY(socket.waitForReadyRead(2000));
    QVERIFY(socket.readAll().contains("monitoring"));
}

QTEST_MAIN(FallGatewayTest)
#include "fall_gateway_test.moc"
```

- [ ] **Step 2: Register the new target and run it to verify it fails**

```cmake
# CMakeLists.txt
add_subdirectory(src/health_falld)
```

```cmake
# src/tests/CMakeLists.txt
add_executable(fall_gateway_test
    fall_daemon_tests/fall_gateway_test.cpp
    ../health_falld/ipc/fall_gateway.cpp
    ../health_falld/runtime/runtime_config.cpp
)
set_target_properties(fall_gateway_test PROPERTIES AUTOMOC ON)
target_include_directories(fall_gateway_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)
target_link_libraries(fall_gateway_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
    ${RK_QT_NETWORK_TARGET}
)
add_test(NAME fall_gateway_test COMMAND fall_gateway_test)
```

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_gateway_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_gateway_test' --output-on-failure
```

Expected: build fails because `health_falld` files do not exist yet.

- [ ] **Step 3: Add the daemon shell, runtime config, and status gateway**

```cpp
// src/health_falld/runtime/runtime_config.h
#pragma once

#include <QString>

struct FallRuntimeConfig {
    QString cameraId = QStringLiteral("front_cam");
    QString socketName = QStringLiteral("rk_fall.sock");
    QString analysisSocketPath = QStringLiteral("/tmp/rk_video_analysis.sock");
    bool enabled = true;
};

FallRuntimeConfig loadFallRuntimeConfig();
```

```cpp
// src/health_falld/ipc/fall_gateway.h
#pragma once

#include "models/fall_models.h"

#include <QObject>

class QLocalServer;

class FallGateway : public QObject {
    Q_OBJECT
public:
    explicit FallGateway(const FallRuntimeStatus &initialStatus, QObject *parent = nullptr);
    bool start();
    void setRuntimeStatus(const FallRuntimeStatus &status);

private:
    void onNewConnection();
    QByteArray buildStatusResponse() const;

    FallRuntimeStatus status_;
    QLocalServer *server_ = nullptr;
};
```

```cpp
// src/health_falld/app/fall_daemon_app.cpp
#include "app/fall_daemon_app.h"

FallDaemonApp::FallDaemonApp(QObject *parent)
    : QObject(parent)
    , config_(loadFallRuntimeConfig())
    , gateway_(new FallGateway(FallRuntimeStatus(), this)) {
}

bool FallDaemonApp::start() {
    FallRuntimeStatus status;
    status.cameraId = config_.cameraId;
    status.inputConnected = false;
    status.poseModelReady = false;
    status.actionModelReady = false;
    status.latestState = QStringLiteral("monitoring");
    gateway_->setRuntimeStatus(status);
    return gateway_->start();
}
```

- [ ] **Step 4: Add the `health-falld` target and verify the test passes**

```cmake
# src/health_falld/CMakeLists.txt
if(NOT RK_QT_CORE_TARGET)
    message(FATAL_ERROR "RK_QT_CORE_TARGET is not set; configure Qt at the top level first.")
endif()

if(RK_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Network)
    set(RK_FALL_NETWORK_TARGET Qt6::Network)
else()
    find_package(Qt5 REQUIRED COMPONENTS Network)
    set(RK_FALL_NETWORK_TARGET Qt5::Network)
endif()

add_executable(health-falld
    main.cpp
    app/fall_daemon_app.cpp
    app/fall_daemon_app.h
    ipc/fall_gateway.cpp
    ipc/fall_gateway.h
    runtime/runtime_config.cpp
    runtime/runtime_config.h
)

set_target_properties(health-falld PROPERTIES AUTOMOC ON)
target_include_directories(health-falld PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(health-falld PRIVATE rk_shared ${RK_QT_CORE_TARGET} ${RK_FALL_NETWORK_TARGET})
```

Run:

```bash
cmake --build /tmp/rk_health_station-build --target health-falld fall_gateway_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_gateway_test' --output-on-failure
```

Expected: `fall_gateway_test` PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add CMakeLists.txt \
  src/health_falld/CMakeLists.txt \
  src/health_falld/main.cpp \
  src/health_falld/app/fall_daemon_app.h \
  src/health_falld/app/fall_daemon_app.cpp \
  src/health_falld/ipc/fall_gateway.h \
  src/health_falld/ipc/fall_gateway.cpp \
  src/health_falld/runtime/runtime_config.h \
  src/health_falld/runtime/runtime_config.cpp \
  src/tests/CMakeLists.txt \
  src/tests/fall_daemon_tests/fall_gateway_test.cpp
git commit -m "feat: add fall daemon skeleton and runtime ipc"
```

### Task 4: Add Analysis Stream Ingest and a Bounded Queue

**Files:**
- Create: `src/health_falld/ingest/analysis_stream_client.h`
- Create: `src/health_falld/ingest/analysis_stream_client.cpp`
- Create: `src/health_falld/action/sequence_buffer.h`
- Create: `src/health_falld/action/sequence_buffer.cpp`
- Test: `src/tests/fall_daemon_tests/analysis_stream_client_test.cpp`
- Test: `src/tests/fall_daemon_tests/sequence_buffer_test.cpp`
- Modify: `src/tests/CMakeLists.txt`
- Modify: `src/health_falld/CMakeLists.txt`

- [ ] **Step 1: Write the failing ingest and sequence tests**

```cpp
// src/tests/fall_daemon_tests/sequence_buffer_test.cpp
#include "action/sequence_buffer.h"

#include <QtTest/QTest>

class SequenceBufferTest : public QObject {
    Q_OBJECT

private slots:
    void keepsOnlyLatestFrames();
};

void SequenceBufferTest::keepsOnlyLatestFrames() {
    SequenceBuffer<int> buffer(3);
    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    buffer.push(4);

    const QVector<int> values = buffer.values();
    QCOMPARE(values.size(), 3);
    QCOMPARE(values.at(0), 2);
    QCOMPARE(values.at(2), 4);
}

QTEST_MAIN(SequenceBufferTest)
#include "sequence_buffer_test.moc"
```

```cpp
// src/tests/fall_daemon_tests/analysis_stream_client_test.cpp
#include "ingest/analysis_stream_client.h"
#include "protocol/analysis_stream_protocol.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QtTest/QTest>

class AnalysisStreamClientTest : public QObject {
    Q_OBJECT

private slots:
    void decodesIncomingFramePackets();
};

void AnalysisStreamClientTest::decodesIncomingFramePackets() {
    QLocalServer server;
    QLocalServer::removeServer(QStringLiteral("rk_video_analysis_test.sock"));
    QVERIFY(server.listen(QStringLiteral("rk_video_analysis_test.sock")));

    AnalysisFramePacket packet;
    packet.frameId = 11;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 640;
    packet.payload = QByteArray("jpeg-bytes");

    AnalysisStreamClient client(QStringLiteral("rk_video_analysis_test.sock"));
    QSignalSpy spy(&client, &AnalysisStreamClient::frameReceived);
    client.start();

    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    socket->write(encodeAnalysisFramePacket(packet));
    socket->flush();

    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(AnalysisStreamClientTest)
#include "analysis_stream_client_test.moc"
```

- [ ] **Step 2: Register the tests and run them to verify they fail**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target sequence_buffer_test analysis_stream_client_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'sequence_buffer_test|analysis_stream_client_test' --output-on-failure
```

Expected: build fails because the client and buffer classes are not implemented yet.

- [ ] **Step 3: Implement the bounded queue and local-socket ingest client**

```cpp
// src/health_falld/action/sequence_buffer.h
#pragma once

#include <QVector>

template <typename T>
class SequenceBuffer {
public:
    explicit SequenceBuffer(int capacity) : capacity_(capacity) {}

    void push(const T &value) {
        if (capacity_ <= 0) {
            return;
        }
        if (values_.size() == capacity_) {
            values_.removeFirst();
        }
        values_.push_back(value);
    }

    QVector<T> values() const { return values_; }
    bool isFull() const { return values_.size() == capacity_; }

private:
    int capacity_ = 0;
    QVector<T> values_;
};
```

```cpp
// src/health_falld/ingest/analysis_stream_client.h
#pragma once

#include "models/fall_models.h"

#include <QObject>

class QLocalSocket;

class AnalysisStreamClient : public QObject {
    Q_OBJECT
public:
    explicit AnalysisStreamClient(const QString &socketName, QObject *parent = nullptr);
    void start();
    void stop();

signals:
    void frameReceived(const AnalysisFramePacket &packet);
    void statusChanged(bool connected);

private:
    void onReadyRead();

    QString socketName_;
    QLocalSocket *socket_ = nullptr;
    QByteArray readBuffer_;
};
```

- [ ] **Step 4: Run the ingest tests to verify they pass**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target sequence_buffer_test analysis_stream_client_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'sequence_buffer_test|analysis_stream_client_test' --output-on-failure
```

Expected: both tests PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/ingest/analysis_stream_client.h \
  src/health_falld/ingest/analysis_stream_client.cpp \
  src/health_falld/action/sequence_buffer.h \
  src/health_falld/action/sequence_buffer.cpp \
  src/health_falld/CMakeLists.txt \
  src/tests/CMakeLists.txt \
  src/tests/fall_daemon_tests/analysis_stream_client_test.cpp \
  src/tests/fall_daemon_tests/sequence_buffer_test.cpp
git commit -m "feat: add fall daemon ingest and bounded queue"
```

### Task 5: Integrate Pose Runtime as a Swappable Interface

**Files:**
- Create: `src/health_falld/pose/pose_types.h`
- Create: `src/health_falld/pose/pose_estimator.h`
- Create: `src/health_falld/pose/rknn_pose_estimator.h`
- Create: `src/health_falld/pose/rknn_pose_estimator.cpp`
- Modify: `src/health_falld/CMakeLists.txt`
- Modify: `src/health_falld/app/fall_daemon_app.cpp`
- Test: `src/tests/fall_daemon_tests/fall_runtime_pose_stub_test.cpp`
- Modify: `src/tests/CMakeLists.txt`

- [ ] **Step 1: Write a failing test around the estimator interface, not the real RKNN runtime**

```cpp
// src/tests/fall_daemon_tests/fall_runtime_pose_stub_test.cpp
#include "pose/pose_estimator.h"

#include <QtTest/QTest>

class FakePoseEstimator : public PoseEstimator {
public:
    bool loadModel(const QString &path, QString *error) override {
        lastPath = path;
        if (error) {
            error->clear();
        }
        return true;
    }

    QVector<PosePerson> infer(const AnalysisFramePacket &frame, QString *error) override {
        Q_UNUSED(frame);
        if (error) {
            error->clear();
        }
        PosePerson person;
        person.score = 0.95f;
        person.keypoints.resize(17);
        return {person};
    }

    QString lastPath;
};

class FallRuntimePoseStubTest : public QObject {
    Q_OBJECT

private slots:
    void estimatorInterfaceReturnsSinglePose();
};

void FallRuntimePoseStubTest::estimatorInterfaceReturnsSinglePose() {
    FakePoseEstimator estimator;
    QString error;
    QVERIFY(estimator.loadModel(QStringLiteral("assets/models/yolov8n-pose.rknn"), &error));

    AnalysisFramePacket frame;
    frame.width = 640;
    frame.height = 640;
    const QVector<PosePerson> result = estimator.infer(frame, &error);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first().keypoints.size(), 17);
}

QTEST_MAIN(FallRuntimePoseStubTest)
#include "fall_runtime_pose_stub_test.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_runtime_pose_stub_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_runtime_pose_stub_test' --output-on-failure
```

Expected: build fails because `PoseEstimator` and `PosePerson` are not defined yet.

- [ ] **Step 3: Add the swappable pose interface and a minimal RKNN wrapper**

```cpp
// src/health_falld/pose/pose_types.h
#pragma once

#include <QRectF>
#include <QVector>

struct PoseKeypoint {
    float x = 0.0f;
    float y = 0.0f;
    float score = 0.0f;
};

struct PosePerson {
    QRectF box;
    float score = 0.0f;
    QVector<PoseKeypoint> keypoints;
};
```

```cpp
// src/health_falld/pose/pose_estimator.h
#pragma once

#include "models/fall_models.h"
#include "pose/pose_types.h"

class PoseEstimator {
public:
    virtual ~PoseEstimator() = default;
    virtual bool loadModel(const QString &path, QString *error) = 0;
    virtual QVector<PosePerson> infer(const AnalysisFramePacket &frame, QString *error) = 0;
};
```

```cpp
// src/health_falld/pose/rknn_pose_estimator.h
#pragma once

#include "pose/pose_estimator.h"

class RknnPoseEstimator : public PoseEstimator {
public:
    bool loadModel(const QString &path, QString *error) override;
    QVector<PosePerson> infer(const AnalysisFramePacket &frame, QString *error) override;

private:
    QString modelPath_;
};
```

- [ ] **Step 4: Run the stub test to verify the interface compiles and passes**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_runtime_pose_stub_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_runtime_pose_stub_test' --output-on-failure
```

Expected: test PASS, even if the real RKNN internals are still skeletal.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/pose/pose_types.h \
  src/health_falld/pose/pose_estimator.h \
  src/health_falld/pose/rknn_pose_estimator.h \
  src/health_falld/pose/rknn_pose_estimator.cpp \
  src/health_falld/CMakeLists.txt \
  src/health_falld/app/fall_daemon_app.cpp \
  src/tests/CMakeLists.txt \
  src/tests/fall_daemon_tests/fall_runtime_pose_stub_test.cpp
git commit -m "feat: add pluggable pose estimator interface"
```

### Task 6: Add ST-GCN Action Classification and Event Policy

**Files:**
- Create: `src/health_falld/action/action_classifier.h`
- Create: `src/health_falld/action/stgcn_action_classifier.h`
- Create: `src/health_falld/action/stgcn_action_classifier.cpp`
- Create: `src/health_falld/action/target_selector.h`
- Create: `src/health_falld/action/target_selector.cpp`
- Create: `src/health_falld/domain/fall_event_policy.h`
- Create: `src/health_falld/domain/fall_event_policy.cpp`
- Create: `src/health_falld/domain/fall_detector_service.h`
- Create: `src/health_falld/domain/fall_detector_service.cpp`
- Test: `src/tests/fall_daemon_tests/fall_event_policy_test.cpp`
- Modify: `src/tests/CMakeLists.txt`
- Modify: `src/health_falld/CMakeLists.txt`

- [ ] **Step 1: Write the failing event-policy test**

```cpp
// src/tests/fall_daemon_tests/fall_event_policy_test.cpp
#include "domain/fall_event_policy.h"

#include <QtTest/QTest>

class FallEventPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void confirmsFallAfterRepeatedLieState();
};

void FallEventPolicyTest::confirmsFallAfterRepeatedLieState() {
    FallEventPolicy policy;

    QVERIFY(!policy.update(QStringLiteral("fall"), 0.90).has_value());
    QVERIFY(!policy.update(QStringLiteral("lie"), 0.93).has_value());
    const auto event = policy.update(QStringLiteral("lie"), 0.95);
    QVERIFY(event.has_value());
    QCOMPARE(event->eventType, QStringLiteral("fall_confirmed"));
}

QTEST_MAIN(FallEventPolicyTest)
#include "fall_event_policy_test.moc"
```

- [ ] **Step 2: Run the event-policy test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_event_policy_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_event_policy_test' --output-on-failure
```

Expected: build fails because `FallEventPolicy` is not implemented yet.

- [ ] **Step 3: Implement the action and policy interfaces with minimal deterministic behavior**

```cpp
// src/health_falld/action/action_classifier.h
#pragma once

#include "pose/pose_types.h"

class ActionClassifier {
public:
    virtual ~ActionClassifier() = default;
    virtual bool loadModel(const QString &path, QString *error) = 0;
    virtual QString classify(const QVector<PosePerson> &people, QString *error) = 0;
};
```

```cpp
// src/health_falld/domain/fall_event_policy.h
#pragma once

#include "models/fall_models.h"

#include <optional>

class FallEventPolicy {
public:
    std::optional<FallEvent> update(const QString &rawState, double confidence);

private:
    int fallLikeCount_ = 0;
};
```

```cpp
// src/health_falld/domain/fall_event_policy.cpp
#include "domain/fall_event_policy.h"

std::optional<FallEvent> FallEventPolicy::update(const QString &rawState, double confidence) {
    if (rawState == QStringLiteral("fall") || rawState == QStringLiteral("lie")) {
        ++fallLikeCount_;
    } else {
        fallLikeCount_ = 0;
    }

    if (fallLikeCount_ < 3) {
        return std::nullopt;
    }

    FallEvent event;
    event.eventType = QStringLiteral("fall_confirmed");
    event.confidence = confidence;
    return event;
}
```

- [ ] **Step 4: Run the policy test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_event_policy_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_event_policy_test' --output-on-failure
```

Expected: test PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/action/action_classifier.h \
  src/health_falld/action/stgcn_action_classifier.h \
  src/health_falld/action/stgcn_action_classifier.cpp \
  src/health_falld/action/target_selector.h \
  src/health_falld/action/target_selector.cpp \
  src/health_falld/domain/fall_event_policy.h \
  src/health_falld/domain/fall_event_policy.cpp \
  src/health_falld/domain/fall_detector_service.h \
  src/health_falld/domain/fall_detector_service.cpp \
  src/health_falld/CMakeLists.txt \
  src/tests/CMakeLists.txt \
  src/tests/fall_daemon_tests/fall_event_policy_test.cpp
git commit -m "feat: add fall action and policy domain"
```

### Task 7: Wire End-to-End Status Flow Without UI Changes

**Files:**
- Modify: `src/health_falld/app/fall_daemon_app.cpp`
- Modify: `src/health_falld/ipc/fall_gateway.cpp`
- Modify: `src/health_falld/ingest/analysis_stream_client.cpp`
- Modify: `src/health_falld/domain/fall_detector_service.cpp`
- Test: `src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
- Modify: `src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing end-to-end status test**

```cpp
// src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
#include "app/fall_daemon_app.h"

#include <QLocalSocket>
#include <QtTest/QTest>

class FallEndToEndStatusTest : public QObject {
    Q_OBJECT

private slots:
    void publishesMonitoringStateWhenStartedWithoutModels();
};

void FallEndToEndStatusTest::publishesMonitoringStateWhenStartedWithoutModels() {
    FallDaemonApp app;
    QVERIFY(app.start());

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("rk_fall.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QVERIFY(socket.waitForReadyRead(2000));

    const QByteArray payload = socket.readAll();
    QVERIFY(payload.contains("monitoring"));
}

QTEST_MAIN(FallEndToEndStatusTest)
#include "fall_end_to_end_status_test.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_end_to_end_status_test' --output-on-failure
```

Expected: build or link fails because the app wiring does not yet connect ingest, service, and gateway state.

- [ ] **Step 3: Connect the daemon app pieces with minimal real behavior**

```cpp
// src/health_falld/app/fall_daemon_app.cpp
bool FallDaemonApp::start() {
    runtimeStatus_.cameraId = config_.cameraId;
    runtimeStatus_.latestState = QStringLiteral("monitoring");
    runtimeStatus_.inputConnected = false;
    runtimeStatus_.poseModelReady = poseEstimator_->loadModel(
        QStringLiteral("assets/models/yolov8n-pose.rknn"), &lastError_);
    runtimeStatus_.actionModelReady = actionClassifier_->loadModel(
        QStringLiteral("assets/models/stgcn_fall.onnx"), &lastError_);
    gateway_->setRuntimeStatus(runtimeStatus_);
    ingestClient_->start();
    return gateway_->start();
}
```

```cpp
// src/health_falld/ingest/analysis_stream_client.cpp
void AnalysisStreamClient::onReadyRead() {
    readBuffer_.append(socket_->readAll());
    AnalysisFramePacket packet;
    if (decodeAnalysisFramePacket(readBuffer_, &packet)) {
        readBuffer_.clear();
        emit frameReceived(packet);
    }
}
```

- [ ] **Step 4: Run the end-to-end status test and a focused regression set**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target \
  health-falld \
  fall_end_to_end_status_test \
  video_service_test \
  video_gateway_test -j4

ctest --test-dir /tmp/rk_health_station-build -R \
  'fall_end_to_end_status_test|video_service_test|video_gateway_test' \
  --output-on-failure
```

Expected: new fall-daemon status test PASS and original targeted video tests still PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/app/fall_daemon_app.cpp \
  src/health_falld/ipc/fall_gateway.cpp \
  src/health_falld/ingest/analysis_stream_client.cpp \
  src/health_falld/domain/fall_detector_service.cpp \
  src/tests/CMakeLists.txt \
  src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
git commit -m "feat: wire fall daemon runtime status flow"
```

### Task 8: Board Validation and Runtime Enablement

**Files:**
- Modify: `config/fall_detection.yaml`
- Copy: `assets/models/yolov8n-pose.rknn`
- Copy: `assets/models/stgcn_fall.onnx`
- Verify: board deployment scripts or manual deploy commands

- [ ] **Step 1: Add the disabled-by-default runtime config**

```yaml
# config/fall_detection.yaml
video:
  analysis:
    enabled: false
    socket_path: /tmp/rk_video_analysis.sock
    width: 640
    height: 640
    fps: 10

fall_detection:
  camera_id: front_cam
  runtime_socket: rk_fall.sock
  pose_model: assets/models/yolov8n-pose.rknn
  action_model: assets/models/stgcn_fall.onnx
  sequence_length: 45
  confirm_frames: 3
```

- [ ] **Step 2: Build the production binaries**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target health-videod health-falld -j4
```

Expected: both binaries build successfully.

- [ ] **Step 3: Deploy the binaries and models to the RK3588 board**

Run:

```bash
ssh -o StrictHostKeyChecking=accept-new -o PreferredAuthentications=password -o PubkeyAuthentication=no elf@192.168.137.179 'mkdir -p ~/rk_health_station/bin ~/rk_health_station/assets/models ~/rk_health_station/config'
scp /tmp/rk_health_station-build/src/health_videod/health-videod elf@192.168.137.179:~/rk_health_station/bin/
scp /tmp/rk_health_station-build/src/health_falld/health-falld elf@192.168.137.179:~/rk_health_station/bin/
scp /home/elf/workspace/rknn_model_zoo-2.1.0/examples/yolov8_pose/model/yolov8n-pose.rknn elf@192.168.137.179:~/rk_health_station/assets/models/
scp /home/elf/workspace/rknn_model_zoo-2.1.0/yolo_detect/stgcn/exports/stgcn_fall.onnx elf@192.168.137.179:~/rk_health_station/assets/models/
scp /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/rk_app/config/fall_detection.yaml elf@192.168.137.179:~/rk_health_station/config/
```

Expected: deployment succeeds.

- [ ] **Step 4: Run a board smoke test with analytics still disabled**

Run:

```bash
ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no elf@192.168.137.179 \
  'cd ~/rk_health_station/bin && ./health-videod >/tmp/health-videod.log 2>&1 & sleep 1; pgrep -a health-videod; pkill health-videod'
```

Expected: `health-videod` starts and exits cleanly exactly as before.

- [ ] **Step 5: Enable analytics, start both services, and verify runtime status**

Run:

```bash
ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no elf@192.168.137.179 \
  "python3 - <<'PY'
from pathlib import Path
path = Path('~/rk_health_station/config/fall_detection.yaml').expanduser()
text = path.read_text()
path.write_text(text.replace('enabled: false', 'enabled: true'))
PY
cd ~/rk_health_station/bin && ./health-videod >/tmp/health-videod.log 2>&1 & \
./health-falld >/tmp/health-falld.log 2>&1 & \
sleep 2; \
python3 - <<'PY'
import socket
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/tmp/rk_fall.sock')
sock.sendall(b'{\"action\":\"get_runtime_status\"}\\n')
print(sock.recv(4096).decode())
sock.close()
PY"
```

Expected: status response includes `front_cam` and a valid runtime state such as `monitoring`.

- [ ] **Step 6: Checkpoint**

If the execution workspace is under git:

```bash
git add config/fall_detection.yaml
git commit -m "chore: add fall detection runtime configuration"
```

## Self-Review Checklist

- Spec coverage:
  - shared protocol split -> Task 1
  - optional video-side analysis output -> Task 2
  - new `health-falld` daemon -> Task 3
  - bounded ingest path and latest-frame policy -> Task 4
  - pose/runtime abstraction -> Task 5
  - ST-GCN and event policy -> Task 6
  - backend-only runtime status wiring -> Task 7
  - board validation and staged enablement -> Task 8
- Placeholder scan:
  - no `TBD`, `TODO`, or "similar to previous task" markers remain
- Type consistency:
  - `AnalysisFramePacket`, `FallRuntimeStatus`, `PoseEstimator`, `SequenceBuffer`, and `FallEventPolicy` names are used consistently across tasks

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-16-rk3588-fall-detection-backend-implementation.md`. Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**

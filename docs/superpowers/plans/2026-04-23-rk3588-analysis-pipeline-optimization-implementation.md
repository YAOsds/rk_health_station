# RK3588 Analysis Pipeline Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the fall-analysis JPEG round-trip by exporting dedicated NV12 analysis frames from `health-videod` while keeping the existing UI preview path unchanged.

**Architecture:** Replace the current preview-derived analysis feed with a dedicated analysis frame branch that originates from the capture graph before `jpegenc`. Keep `health-videod` as the only camera owner, keep `health-ui` unchanged, and update `health_falld` to preprocess `NV12` analysis packets directly into RKNN input tensors.

**Tech Stack:** Qt 5/6 Core/Network/Gui, existing local-socket protocols, GStreamer runtime integration inside `health-videod`, Qt Test / CTest, RKNN pose runtime.

---

## File Structure Map

### Shared protocol and model files
- Modify: `rk_app/src/shared/models/fall_models.h` - make `AnalysisFramePacket` a first-class carrier for `NV12` frames and expose enough metadata for preprocessing.
- Modify: `rk_app/src/shared/protocol/analysis_stream_protocol.h` - keep the public protocol API stable while extending packet semantics.
- Modify: `rk_app/src/shared/protocol/analysis_stream_protocol.cpp` - preserve existing framing while validating the new `NV12` metadata path.
- Modify: `rk_app/src/tests/protocol_tests/analysis_stream_protocol_test.cpp` - add protocol tests for `NV12` payloads and mixed-format decoding.

### `health-videod` production files
- Create: `rk_app/src/health_videod/analysis/analysis_frame_source.h` - declare an in-process analysis frame tap interface used by the pipeline backend to publish raw frames.
- Create: `rk_app/src/health_videod/analysis/analysis_frame_source.cpp` - implement small utility helpers for frame publication and buffering.
- Modify: `rk_app/src/health_videod/analysis/analysis_output_backend.h` - separate stream publication from preview URL consumption and define a push-based frame publication contract.
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h` - convert the backend into a publisher/distributor of already-captured analysis frames.
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp` - remove preview socket/MJPEG parsing and replace it with packet broadcast of `NV12` frames.
- Modify: `rk_app/src/health_videod/pipeline/video_pipeline_backend.h` - allow the pipeline backend to accept an optional analysis frame sink.
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h` - store the analysis sink and any in-process GStreamer objects needed for the analysis branch.
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp` - replace preview-derived analysis export with a capture-graph branch that emits `NV12` frames before `jpegenc`.
- Modify: `rk_app/src/health_videod/core/video_service.cpp` - keep preview behavior unchanged while synchronizing the new analysis export lifecycle.
- Modify: `rk_app/src/health_videod/core/video_service.h` - wire the new analysis synchronization helpers without changing UI-facing video APIs.
- Modify: `rk_app/src/health_videod/CMakeLists.txt` - link the required GStreamer/appsink pieces for the new analysis branch.

### `health-falld` production files
- Create: `rk_app/src/health_falld/pose/nv12_preprocessor.h` - declare deterministic NV12-to-model preprocessing helpers.
- Create: `rk_app/src/health_falld/pose/nv12_preprocessor.cpp` - implement resize/letterbox/color-conversion helpers that consume `AnalysisFramePacket` with `NV12` payloads.
- Modify: `rk_app/src/health_falld/pose/pose_estimator.h` - keep the estimator interface stable while clarifying accepted frame formats.
- Modify: `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp` - add the new `NV12` input path and retain JPEG support only as a temporary compatibility fallback.
- Modify: `rk_app/src/health_falld/CMakeLists.txt` - compile and link the new preprocessor helpers.

### Test files
- Modify: `rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp` - replace preview-MJPEG assumptions with direct frame publication tests.
- Modify: `rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp` - assert the client decodes `NV12` frame packets and mixed payload sequences.
- Create: `rk_app/src/tests/fall_daemon_tests/nv12_preprocessor_test.cpp` - verify deterministic NV12 preprocessing output dimensions and padding.
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_runtime_pose_stub_test.cpp` - add a regression test proving the fall runtime accepts `NV12` packets without JPEG decode.
- Modify: `rk_app/src/tests/video_daemon_tests/video_service_analysis_test.cpp` - validate that analysis startup no longer depends on `previewUrl` semantics.
- Modify: `rk_app/src/tests/CMakeLists.txt` - register the new/updated tests and required sources.

---

### Task 1: Extend the analysis packet model from preview-JPEG-only semantics to first-class NV12 transport

**Files:**
- Modify: `rk_app/src/shared/models/fall_models.h`
- Modify: `rk_app/src/shared/protocol/analysis_stream_protocol.h`
- Modify: `rk_app/src/shared/protocol/analysis_stream_protocol.cpp`
- Modify: `rk_app/src/tests/protocol_tests/analysis_stream_protocol_test.cpp`

- [ ] **Step 1: Write the failing protocol test for an NV12 packet**

```cpp
void AnalysisStreamProtocolTest::roundTripsNv12FramePacket() {
    AnalysisFramePacket packet;
    packet.frameId = 1001;
    packet.timestampMs = 1777000000000;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 480;
    packet.pixelFormat = AnalysisPixelFormat::Nv12;
    packet.payload = QByteArray(640 * 480 * 3 / 2, '\\x7f');

    const QByteArray encoded = encodeAnalysisFramePacket(packet);
    AnalysisFramePacket decoded;
    QVERIFY(decodeAnalysisFramePacket(encoded, &decoded));
    QCOMPARE(decoded.frameId, packet.frameId);
    QCOMPARE(decoded.cameraId, packet.cameraId);
    QCOMPARE(decoded.width, 640);
    QCOMPARE(decoded.height, 480);
    QCOMPARE(decoded.pixelFormat, AnalysisPixelFormat::Nv12);
    QCOMPARE(decoded.payload.size(), packet.payload.size());
    QCOMPARE(decoded.payload, packet.payload);
}
```

- [ ] **Step 2: Run the protocol test to verify current semantics are underspecified**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target analysis_stream_protocol_test && ctest --test-dir out/build-rk_app-host-tests -R analysis_stream_protocol_test --output-on-failure`

Expected: the new `roundTripsNv12FramePacket` test is missing and must be added before the target can pass.

- [ ] **Step 3: Update the packet model to make NV12 explicit**

```cpp
// rk_app/src/shared/models/fall_models.h

enum class AnalysisPixelFormat {
    Jpeg = 0,
    Nv12 = 1,
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
```

Keep the public struct shape stable. The important change in this task is not adding new fields; it is changing the meaning of `AnalysisFramePacket` so `Nv12` becomes a normal, tested packet type rather than an unused enum value.

- [ ] **Step 4: Add a mixed-format regression test to protect backward compatibility**

```cpp
void AnalysisStreamProtocolTest::takesFirstPacketFromMixedFormatStream() {
    AnalysisFramePacket first;
    first.frameId = 1;
    first.cameraId = QStringLiteral("front_cam");
    first.width = 640;
    first.height = 480;
    first.pixelFormat = AnalysisPixelFormat::Jpeg;
    first.payload = QByteArray("jpeg-bytes");

    AnalysisFramePacket second;
    second.frameId = 2;
    second.cameraId = QStringLiteral("front_cam");
    second.width = 640;
    second.height = 480;
    second.pixelFormat = AnalysisPixelFormat::Nv12;
    second.payload = QByteArray(640 * 480 * 3 / 2, '\\x10');

    QByteArray stream = encodeAnalysisFramePacket(first) + encodeAnalysisFramePacket(second);
    AnalysisFramePacket decoded;
    QVERIFY(takeFirstAnalysisFramePacket(&stream, &decoded));
    QCOMPARE(decoded.pixelFormat, AnalysisPixelFormat::Jpeg);
    QVERIFY(takeFirstAnalysisFramePacket(&stream, &decoded));
    QCOMPARE(decoded.pixelFormat, AnalysisPixelFormat::Nv12);
    QCOMPARE(decoded.payload.size(), second.payload.size());
}
```

- [ ] **Step 5: Run the protocol test target and verify it passes**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target analysis_stream_protocol_test && ctest --test-dir out/build-rk_app-host-tests -R analysis_stream_protocol_test --output-on-failure`

Expected: `analysis_stream_protocol_test` passes with the new `NV12` packet coverage.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/shared/models/fall_models.h \
        rk_app/src/shared/protocol/analysis_stream_protocol.h \
        rk_app/src/shared/protocol/analysis_stream_protocol.cpp \
        rk_app/src/tests/protocol_tests/analysis_stream_protocol_test.cpp
git commit -m "refactor: make analysis frame packets nv12-ready"
```

### Task 2: Replace preview-derived analysis export with a push-based analysis frame publisher inside `health-videod`

**Files:**
- Create: `rk_app/src/health_videod/analysis/analysis_frame_source.h`
- Create: `rk_app/src/health_videod/analysis/analysis_frame_source.cpp`
- Modify: `rk_app/src/health_videod/analysis/analysis_output_backend.h`
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h`
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/video_service_analysis_test.cpp`

- [ ] **Step 1: Write the failing backend test for direct frame publication**

```cpp
void AnalysisOutputBackendTest::forwardsPublishedNv12FrameToLocalSocket() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_backend_test.sock"));

    GstreamerAnalysisOutputBackend backend;
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;

    QString error;
    QVERIFY(backend.start(status, &error));
    QVERIFY(error.isEmpty());

    QLocalSocket client;
    client.connectToServer(QStringLiteral("/tmp/rk_video_analysis_backend_test.sock"));
    QVERIFY(client.waitForConnected(2000));

    AnalysisFramePacket packet;
    packet.frameId = 7;
    packet.timestampMs = 1777000000123;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 480;
    packet.pixelFormat = AnalysisPixelFormat::Nv12;
    packet.payload = QByteArray(640 * 480 * 3 / 2, '\\x55');

    backend.publishFrame(packet);

    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() > 0 || client.waitForReadyRead(50), 2000);
    AnalysisFramePacket decoded;
    QVERIFY(decodeAnalysisFramePacket(client.readAll(), &decoded));
    QCOMPARE(decoded.pixelFormat, AnalysisPixelFormat::Nv12);
    QCOMPARE(decoded.payload, packet.payload);

    backend.stop(QStringLiteral("front_cam"), &error);
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}
```

- [ ] **Step 2: Run the backend tests to verify the new publisher API does not exist yet**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target analysis_output_backend_test video_service_analysis_test && ctest --test-dir out/build-rk_app-host-tests -R "analysis_output_backend_test|video_service_analysis_test" --output-on-failure`

Expected: build fails because `publishFrame()` and the new source abstraction do not exist yet.

- [ ] **Step 3: Introduce a push-based analysis frame source interface**

```cpp
// rk_app/src/health_videod/analysis/analysis_frame_source.h
#pragma once

#include "models/fall_models.h"

class AnalysisFrameSink {
public:
    virtual ~AnalysisFrameSink() = default;
    virtual void publishFrame(const AnalysisFramePacket &packet) = 0;
};
```

```cpp
// rk_app/src/health_videod/analysis/analysis_output_backend.h
#pragma once

#include "analysis/analysis_frame_source.h"
#include "models/fall_models.h"
#include "models/video_models.h"

#include <QString>

class AnalysisOutputBackend : public AnalysisFrameSink {
public:
    virtual ~AnalysisOutputBackend() = default;

    virtual bool start(const VideoChannelStatus &status, QString *error) = 0;
    virtual bool stop(const QString &cameraId, QString *error) = 0;
    virtual AnalysisChannelStatus statusForCamera(const QString &cameraId) const = 0;
};
```

- [ ] **Step 4: Convert `GstreamerAnalysisOutputBackend` into a packet broadcaster**

```cpp
// rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp
void GstreamerAnalysisOutputBackend::publishFrame(const AnalysisFramePacket &packet) {
    if (activeCameraId_.isEmpty() || packet.cameraId != activeCameraId_) {
        return;
    }

    AnalysisChannelStatus status = statuses_.value(activeCameraId_, defaultStatusForCamera(activeCameraId_));
    status.streamConnected = true;
    status.outputFormat = packet.pixelFormat == AnalysisPixelFormat::Nv12
        ? QStringLiteral("nv12")
        : QStringLiteral("jpeg");
    status.width = packet.width;
    status.height = packet.height;
    statuses_.insert(activeCameraId_, status);

    const QByteArray encoded = encodeAnalysisFramePacket(packet);
    for (int i = clients_.size() - 1; i >= 0; --i) {
        QLocalSocket *client = clients_.at(i);
        if (!client || client->state() != QLocalSocket::ConnectedState) {
            clients_.removeAt(i);
            continue;
        }
        client->write(encoded);
        client->flush();
    }
}
```

At the same time, delete the preview-derived responsibilities:

- remove `previewSocket_`
- remove `configurePreviewSource()`
- remove `processPreviewChunk()`
- remove MJPEG boundary parsing from this backend

The backend's job after this task is only:

- listen for local analysis consumers
- publish already-produced `AnalysisFramePacket` objects
- report analysis stream status

- [ ] **Step 5: Update the video-service analysis test to assert preview independence**

```cpp
class FakeAnalysisOutputBackend : public AnalysisOutputBackend {
public:
    bool start(const VideoChannelStatus &status, QString *error) override {
        lastStartedStatus = status;
        if (error) {
            error->clear();
        }
        started = true;
        return true;
    }

    bool stop(const QString &cameraId, QString *error) override {
        lastStoppedCameraId = cameraId;
        if (error) {
            error->clear();
        }
        stopped = true;
        return true;
    }

    void publishFrame(const AnalysisFramePacket &packet) override {
        lastPublishedFrame = packet;
    }

    AnalysisChannelStatus statusForCamera(const QString &cameraId) const override {
        AnalysisChannelStatus status;
        status.cameraId = cameraId;
        status.enabled = started && !stopped;
        status.streamConnected = started && !stopped;
        status.outputFormat = QStringLiteral("nv12");
        status.width = 640;
        status.height = 480;
        status.fps = 30;
        return status;
    }

    bool started = false;
    bool stopped = false;
    QString lastStoppedCameraId;
    VideoChannelStatus lastStartedStatus;
    AnalysisFramePacket lastPublishedFrame;
};
```

Add a new test:

```cpp
void VideoServiceAnalysisTest::analysisStartDoesNotRequirePreviewUrl() {
    qputenv("RK_VIDEO_ANALYSIS_ENABLED", QByteArray("1"));
    FakeAnalysisOutputBackend analysisBackend;
    VideoService service(nullptr, &analysisBackend, nullptr);

    QString errorCode;
    QVERIFY(service.startPreview(QStringLiteral("front_cam")).ok);
    QVERIFY(analysisBackend.started);
    QCOMPARE(analysisBackend.lastStartedStatus.cameraId, QStringLiteral("front_cam"));
    qunsetenv("RK_VIDEO_ANALYSIS_ENABLED");
}
```

The point is to keep the API stable while changing the backend expectation: analysis start is still synchronized with preview lifecycle, but the backend itself must no longer require `previewUrl` semantics.

- [ ] **Step 6: Run the updated backend tests and verify they pass**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target analysis_output_backend_test video_service_analysis_test && ctest --test-dir out/build-rk_app-host-tests -R "analysis_output_backend_test|video_service_analysis_test" --output-on-failure`

Expected: both targets pass with the new push-based analysis publisher design.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_videod/analysis/analysis_frame_source.h \
        rk_app/src/health_videod/analysis/analysis_frame_source.cpp \
        rk_app/src/health_videod/analysis/analysis_output_backend.h \
        rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h \
        rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp \
        rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp \
        rk_app/src/tests/video_daemon_tests/video_service_analysis_test.cpp
git commit -m "refactor: decouple analysis output from preview mjpeg"
```

### Task 3: Refactor `health-videod` pipeline management so analysis frames are emitted from the capture graph before `jpegenc`

**Files:**
- Modify: `rk_app/src/health_videod/pipeline/video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/health_videod/core/video_service.h`
- Modify: `rk_app/src/health_videod/core/video_service.cpp`
- Modify: `rk_app/src/health_videod/CMakeLists.txt`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`

- [ ] **Step 1: Write the failing pipeline-backend test for an analysis branch in preview mode**

```cpp
void GstreamerVideoPipelineBackendTest::buildsPreviewPipelineWithNv12AnalysisBranchWhenSinkPresent() {
    GstreamerVideoPipelineBackend backend;
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    const QString command = backend.buildPreviewCommandForTest(status, true);
    QVERIFY(command.contains(QStringLiteral("tee name=t")));
    QVERIFY(command.contains(QStringLiteral("jpegenc ! multipartmux boundary=rkpreview")));
    QVERIFY(command.contains(QStringLiteral("appsink name=analysis_sink")));
}
```

- [ ] **Step 2: Run the backend test to verify the preview command cannot yet expose an analysis branch**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target gstreamer_video_pipeline_backend_test && ctest --test-dir out/build-rk_app-host-tests -R gstreamer_video_pipeline_backend_test --output-on-failure`

Expected: build fails because the new test helper and analysis-branch behavior do not exist.

- [ ] **Step 3: Extend the pipeline-backend interface to accept an optional analysis sink**

```cpp
// rk_app/src/health_videod/pipeline/video_pipeline_backend.h
#pragma once

#include "analysis/analysis_frame_source.h"
#include "models/video_models.h"

#include <QString>

class VideoPipelineObserver {
public:
    virtual ~VideoPipelineObserver() = default;
    virtual void onPipelinePlaybackFinished(const QString &cameraId) = 0;
    virtual void onPipelineRuntimeError(const QString &cameraId, const QString &error) = 0;
};

class VideoPipelineBackend {
public:
    virtual ~VideoPipelineBackend() = default;
    virtual void setObserver(VideoPipelineObserver *observer) = 0;
    virtual void setAnalysisSink(AnalysisFrameSink *sink) = 0;
    virtual bool startPreview(const VideoChannelStatus &status, QString *previewUrl, QString *error) = 0;
    virtual bool stopPreview(const QString &cameraId, QString *error) = 0;
    virtual bool captureSnapshot(const VideoChannelStatus &status, const QString &outputPath, QString *error) = 0;
    virtual bool startRecording(const VideoChannelStatus &status, const QString &outputPath, QString *error) = 0;
    virtual bool stopRecording(const QString &cameraId, QString *error) = 0;
};
```

Then wire the sink in `VideoService`:

```cpp
VideoService::VideoService(
    VideoPipelineBackend *pipelineBackend, AnalysisOutputBackend *analysisBackend, QObject *parent)
    : QObject(parent)
    , pipelineBackend_(pipelineBackend ? pipelineBackend : new GstreamerVideoPipelineBackend())
    , analysisBackend_(analysisBackend ? analysisBackend : new GstreamerAnalysisOutputBackend())
    , ownsPipelineBackend_(!pipelineBackend)
    , ownsAnalysisBackend_(!analysisBackend) {
    pipelineBackend_->setObserver(this);
    pipelineBackend_->setAnalysisSink(analysisBackend_);
    initializeDefaultChannels();
}
```

- [ ] **Step 4: Replace shell-only preview-derived analysis with an in-process GStreamer analysis branch**

Implement the preview pipeline so the analysis path branches before `jpegenc`.

Expected graph shape:

```text
v4l2src -> capsfilter(NV12) -> tee name=t
    t. -> queue -> jpegenc -> multipartmux -> tcpserversink   (existing UI preview)
    t. -> queue -> appsink name=analysis_sink                 (new analysis branch)
```

Expected recording graph shape:

```text
v4l2src -> capsfilter(NV12/record profile) -> tee name=t
    t. -> queue -> videoscale -> preview caps -> jpegenc -> multipartmux -> tcpserversink
    t. -> queue -> mpph264enc -> h264parse -> qtmux -> filesink
    t. -> queue -> appsink name=analysis_sink
```

Use an in-process GStreamer graph for preview/recording mode so the analysis branch can feed `AnalysisFrameSink::publishFrame()` directly from appsink callbacks. Do not keep using preview TCP output as the analysis source.

At the end of this step, `GstreamerVideoPipelineBackend` must:

- preserve the same preview URL contract for `health-ui`
- preserve the same record output contract for files
- emit `AnalysisFramePacket` with `pixelFormat = AnalysisPixelFormat::Nv12`
- never reconnect to `previewUrl` to build analysis packets

- [ ] **Step 5: Update the pipeline backend tests for both preview and recording paths**

Add a second test:

```cpp
void GstreamerVideoPipelineBackendTest::buildsRecordingPipelineWithPreviewRecordAndAnalysisBranches() {
    GstreamerVideoPipelineBackend backend;
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");
    status.recordProfile.width = 1280;
    status.recordProfile.height = 720;
    status.recordProfile.fps = 30;
    status.recordProfile.pixelFormat = QStringLiteral("NV12");

    const QString command = backend.buildRecordingCommandForTest(status, QStringLiteral("/tmp/out.mp4"), true);
    QVERIFY(command.contains(QStringLiteral("tee name=t")));
    QVERIFY(command.contains(QStringLiteral("mpph264enc")));
    QVERIFY(command.contains(QStringLiteral("appsink name=analysis_sink")));
}
```

- [ ] **Step 6: Run the video-daemon backend tests and verify they pass**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target gstreamer_video_pipeline_backend_test video_service_test video_daemon_shutdown_test && ctest --test-dir out/build-rk_app-host-tests -R "gstreamer_video_pipeline_backend_test|video_service_test|video_daemon_shutdown_test" --output-on-failure`

Expected: all three targets pass, proving preview compatibility and lifecycle stability remain intact.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_videod/pipeline/video_pipeline_backend.h \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp \
        rk_app/src/health_videod/core/video_service.h \
        rk_app/src/health_videod/core/video_service.cpp \
        rk_app/src/health_videod/CMakeLists.txt \
        rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp
git commit -m "refactor: export nv12 analysis frames from capture graph"
```

### Task 4: Teach `health_falld` to preprocess NV12 packets directly and keep JPEG only as a temporary compatibility fallback

**Files:**
- Create: `rk_app/src/health_falld/pose/nv12_preprocessor.h`
- Create: `rk_app/src/health_falld/pose/nv12_preprocessor.cpp`
- Modify: `rk_app/src/health_falld/pose/pose_estimator.h`
- Modify: `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp`
- Modify: `rk_app/src/health_falld/CMakeLists.txt`
- Create: `rk_app/src/tests/fall_daemon_tests/nv12_preprocessor_test.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_runtime_pose_stub_test.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing preprocessor test for NV12 frame preparation**

```cpp
void Nv12PreprocessorTest::producesRgbTensorSizedBufferFromNv12Packet() {
    AnalysisFramePacket frame;
    frame.frameId = 88;
    frame.cameraId = QStringLiteral("front_cam");
    frame.width = 4;
    frame.height = 4;
    frame.pixelFormat = AnalysisPixelFormat::Nv12;
    frame.payload = QByteArray(4 * 4 * 3 / 2, '\\x80');

    QString error;
    const QByteArray packed = preprocessNv12ForPose(frame, 8, 8, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(packed.size(), 8 * 8 * 3);
}
```

- [ ] **Step 2: Run the fall-daemon test target to verify the NV12 preprocessor does not exist yet**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target analysis_stream_client_test fall_runtime_pose_stub_test && ctest --test-dir out/build-rk_app-host-tests -R "analysis_stream_client_test|fall_runtime_pose_stub_test" --output-on-failure`

Expected: the new preprocessor test target is missing and the existing fall runtime still assumes JPEG payloads.

- [ ] **Step 3: Add a dedicated NV12 preprocessing helper**

```cpp
// rk_app/src/health_falld/pose/nv12_preprocessor.h
#pragma once

#include "models/fall_models.h"

#include <QByteArray>
#include <QString>

QByteArray preprocessNv12ForPose(
    const AnalysisFramePacket &frame, int targetWidth, int targetHeight, QString *error);

QByteArray preprocessRgbImageForPose(
    const QImage &image, int targetWidth, int targetHeight, QString *error);
```

Implementation responsibilities in `nv12_preprocessor.cpp`:

- validate `pixelFormat == AnalysisPixelFormat::Nv12`
- validate payload length is `width * height * 3 / 2`
- convert the NV12 payload into an RGB888 staging image
- apply letterbox resize into the target model size
- pack the result into contiguous RGB bytes for RKNN

- [ ] **Step 4: Update `RknnPoseEstimator::infer()` to prefer NV12 packets**

```cpp
QVector<PosePerson> RknnPoseEstimator::infer(const AnalysisFramePacket &frame, QString *error) {
#ifdef RKAPP_ENABLE_REAL_RKNN_POSE
    auto *runtime = static_cast<RknnPoseRuntime *>(runtime_);
    if (!runtime) {
        if (error) {
            *error = QStringLiteral("pose_model_not_loaded");
        }
        return {};
    }

    QByteArray packed;
    if (frame.pixelFormat == AnalysisPixelFormat::Nv12) {
        packed = preprocessNv12ForPose(frame, runtime->appCtx.model_width, runtime->appCtx.model_height, error);
        if (!error->isEmpty()) {
            return {};
        }
    } else {
        QImage inputImage = QImage::fromData(frame.payload, "JPEG").convertToFormat(QImage::Format_RGB888);
        if (inputImage.isNull()) {
            if (error) {
                *error = QStringLiteral("pose_input_decode_failed");
            }
            return {};
        }

        AnalysisFramePacket jpegCompatFrame = frame;
        jpegCompatFrame.width = inputImage.width();
        jpegCompatFrame.height = inputImage.height();
        packed = preprocessRgbImageForPose(inputImage, runtime->appCtx.model_width, runtime->appCtx.model_height, error);
        if (!error->isEmpty()) {
            return {};
        }
    }

    rknn_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    input.size = packed.size();
    input.buf = packed.data();
    // existing RKNN run path continues unchanged
```

The design intent in this task is clear:

- `NV12` becomes the normal path
- `JPEG` remains only as a compatibility fallback while migration is in progress

- [ ] **Step 5: Add runtime tests that prove the fall path accepts NV12 packets**

Add to `analysis_stream_client_test.cpp`:

```cpp
void AnalysisStreamClientTest::decodesNv12FramePacket() {
    QLocalServer server;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_nv12_test.sock"));
    QVERIFY(server.listen(QStringLiteral("/tmp/rk_video_analysis_nv12_test.sock")));

    AnalysisFramePacket packet;
    packet.frameId = 55;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 480;
    packet.pixelFormat = AnalysisPixelFormat::Nv12;
    packet.payload = QByteArray(640 * 480 * 3 / 2, '\\x33');

    AnalysisStreamClient client(QStringLiteral("/tmp/rk_video_analysis_nv12_test.sock"));
    QSignalSpy spy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    client.start();

    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);
    socket->write(encodeAnalysisFramePacket(packet));
    socket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 1, 2000);
}
```

Add to `fall_runtime_pose_stub_test.cpp` a regression that injects an `NV12` packet and verifies the runtime path still produces a classification update through the stub estimator.

- [ ] **Step 6: Run the fall-daemon tests and verify they pass**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target analysis_stream_client_test fall_runtime_pose_stub_test nv12_preprocessor_test && ctest --test-dir out/build-rk_app-host-tests -R "analysis_stream_client_test|fall_runtime_pose_stub_test|nv12_preprocessor_test" --output-on-failure`

Expected: all targets pass, proving `health_falld` can ingest `NV12` analysis frames without requiring JPEG decode.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_falld/pose/nv12_preprocessor.h \
        rk_app/src/health_falld/pose/nv12_preprocessor.cpp \
        rk_app/src/health_falld/pose/pose_estimator.h \
        rk_app/src/health_falld/pose/rknn_pose_estimator.cpp \
        rk_app/src/health_falld/CMakeLists.txt \
        rk_app/src/tests/fall_daemon_tests/nv12_preprocessor_test.cpp \
        rk_app/src/tests/fall_daemon_tests/fall_runtime_pose_stub_test.cpp \
        rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp \
        rk_app/src/tests/CMakeLists.txt
git commit -m "feat: feed fall detection with nv12 analysis frames"
```

### Task 5: Verify end-to-end separation and document the new analysis path

**Files:**
- Modify: `docs/architecture/camera-video-readme.md`
- Modify: `docs/superpowers/specs/2026-04-22-rk3588-analysis-pipeline-optimization-design.md`
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`

- [ ] **Step 1: Write the failing end-to-end regression for preview-independent analysis**

Add a new test case that simulates analysis frames arriving over the dedicated analysis socket while the fall runtime remains disconnected from any preview MJPEG assumptions.

```cpp
void FallEndToEndStatusTest::acceptsNv12AnalysisPacketsWithoutPreviewTransport() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e_nv12.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e_nv12.sock")));

    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e_nv12.sock"));
    // start fall runtime here as existing tests do
    // write one NV12 packet and wait for runtime status / classification output
}
```

- [ ] **Step 2: Run the end-to-end fall test to verify the new coverage fails before the implementation is complete**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target fall_end_to_end_status_test && ctest --test-dir out/build-rk_app-host-tests -R fall_end_to_end_status_test --output-on-failure`

Expected: the new `NV12` end-to-end case is missing and must be added before the target can fully cover the redesigned data path.

- [ ] **Step 3: Update the architecture documentation to describe the new path**

Update `docs/architecture/camera-video-readme.md` so it explicitly says:

```markdown
- UI preview remains `tcp_mjpeg` in this phase.
- Fall analysis no longer derives frames from `preview_url`.
- `health-videod` exports dedicated `NV12` analysis frames from the capture graph before `jpegenc`.
- `health-falld` preprocesses those frames directly for RKNN inference.
```

Also update the approved optimization spec with an implementation note recording that the first delivery keeps the preview path unchanged while analysis switches to `NV12` transport.

- [ ] **Step 4: Run the end-to-end regression and verify it passes**

Run: `cmake --build out/build-rk_app-host-tests -j4 --target fall_end_to_end_status_test && ctest --test-dir out/build-rk_app-host-tests -R fall_end_to_end_status_test --output-on-failure`

Expected: the end-to-end fall test suite passes, including the new `NV12` transport case.

- [ ] **Step 5: Run the final focused verification suite**

Run:

```bash
cmake --build out/build-rk_app-host-tests -j4 --target \
  analysis_stream_protocol_test \
  analysis_output_backend_test \
  gstreamer_video_pipeline_backend_test \
  analysis_stream_client_test \
  nv12_preprocessor_test \
  fall_runtime_pose_stub_test \
  fall_end_to_end_status_test
ctest --test-dir out/build-rk_app-host-tests -R \
  "analysis_stream_protocol_test|analysis_output_backend_test|gstreamer_video_pipeline_backend_test|analysis_stream_client_test|nv12_preprocessor_test|fall_runtime_pose_stub_test|fall_end_to_end_status_test" \
  --output-on-failure
```

Expected: all focused targets pass, proving that:

- preview behavior remains intact from the backend contract perspective
- analysis packets are now `NV12`-capable
- the analysis path no longer depends on preview MJPEG semantics
- fall inference accepts the new transport path

- [ ] **Step 6: Commit**

```bash
git add docs/architecture/camera-video-readme.md \
        docs/superpowers/specs/2026-04-22-rk3588-analysis-pipeline-optimization-design.md \
        rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
git commit -m "test: verify preview-independent nv12 analysis pipeline"
```

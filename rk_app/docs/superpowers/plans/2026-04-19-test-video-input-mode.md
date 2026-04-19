# Test Video Input Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Qt-driven test mode that temporarily switches the video monitor page from live camera input to a user-selected MP4 file, while preserving the existing ByteTrack fall-detection pipeline.

**Architecture:** Keep `health-videod` as the sole owner of video input mode. Extend shared video status/IPC so the UI can request test-mode entry and render backend-owned mode state. Generalize the preview backend to support file input and notify `VideoService` when MP4 playback finishes, while `health-falld` remains unchanged and keeps consuming analysis frames from the existing analysis socket.

**Tech Stack:** C++17, Qt Core/Network/Widgets/Test, existing local video IPC, GStreamer preview pipeline, current RK3588 `health-videod` / `health-falld` architecture.

---

## File Structure

### Existing files to modify

- `rk_app/src/shared/models/video_models.h`
  - Extend `VideoChannelStatus` with backend-owned test-mode fields.
- `rk_app/src/shared/protocol/video_ipc.h`
  - Keep the command/result types unchanged structurally, but expose the richer status payload through serialization helpers.
- `rk_app/src/shared/protocol/video_ipc.cpp`
  - Serialize and parse new status fields.
- `rk_app/src/health_ui/ipc_client/video_ipc_client.h`
  - Extend `AbstractVideoClient` / `VideoIpcClient` with test-mode actions.
- `rk_app/src/health_ui/ipc_client/video_ipc_client.cpp`
  - Send `start_test_input` / `stop_test_input` commands.
- `rk_app/src/health_videod/core/video_service.h`
  - Add test-mode entry/exit APIs and observer hooks for playback-finished events.
- `rk_app/src/health_videod/core/video_service.cpp`
  - Validate files, switch modes, gate snapshot/recording in test mode, and hold `finished` state after EOF.
- `rk_app/src/health_videod/ipc/video_gateway.cpp`
  - Route the new actions.
- `rk_app/src/health_videod/pipeline/video_pipeline_backend.h`
  - Add a small observer contract so backends can notify `VideoService` when a test file finishes.
- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
  - Store observer pointer and split camera/file command builders.
- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
  - Build MP4 preview pipelines and emit playback-finished notifications for test-file sessions.
- `rk_app/src/health_ui/pages/video_monitor_page.h`
  - Add test-mode labels, buttons, and a file-picker callback injection point.
- `rk_app/src/health_ui/pages/video_monitor_page.cpp`
  - Wire file selection, request backend switches, and render mode/file state.
- `rk_app/src/health_ui/widgets/video_preview_widget.h`
  - Add a dedicated source badge API separate from the fall-classification overlay.
- `rk_app/src/health_ui/widgets/video_preview_widget.cpp`
  - Render `TEST MODE` + filename badge without interfering with person-state labels.
- `rk_app/src/health_ui/app/ui_app.cpp`
  - Construct `VideoMonitorPage` with the default file-picker lambda.

### Test files to modify

- `rk_app/src/tests/protocol_tests/video_protocol_test.cpp`
- `rk_app/src/tests/video_daemon_tests/video_service_test.cpp`
- `rk_app/src/tests/ipc_tests/video_gateway_test.cpp`
- `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`
- `rk_app/src/tests/ui_tests/video_preview_widget_test.cpp`

### Notes for decomposition

- Do **not** modify `health-falld` for this feature.
- Keep all source-mode knowledge inside shared video models/protocol, `health-videod`, and `health-ui`.
- Reuse `VideoChannelStatus` as the single source of truth instead of inventing a second channel or UI-only playback path.

### Build / test workspace

- Host build directory: `/tmp/rk_health_station-test-video-input-mode-build`
- Base smoke commands used throughout this plan:
  - `cmake --build /tmp/rk_health_station-test-video-input-mode-build -j4`
  - `ctest --test-dir /tmp/rk_health_station-test-video-input-mode-build --output-on-failure`

---

### Task 1: Extend shared status and UI IPC surface for test mode

**Files:**
- Modify: `rk_app/src/shared/models/video_models.h`
- Modify: `rk_app/src/shared/protocol/video_ipc.cpp`
- Modify: `rk_app/src/health_ui/ipc_client/video_ipc_client.h`
- Modify: `rk_app/src/health_ui/ipc_client/video_ipc_client.cpp`
- Test: `rk_app/src/tests/protocol_tests/video_protocol_test.cpp`

- [ ] **Step 1: Write the failing protocol test for test-mode fields and commands**

```cpp
void VideoProtocolTest::roundTripsTestInputStatusPayload() {
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.inputMode = QStringLiteral("test_file");
    status.testFilePath = QStringLiteral("/tmp/fall-demo.mp4");
    status.testPlaybackState = QStringLiteral("finished");

    const QJsonObject json = videoChannelStatusToJson(status);
    VideoChannelStatus decoded;
    QVERIFY(videoChannelStatusFromJson(json, &decoded));
    QCOMPARE(decoded.inputMode, QStringLiteral("test_file"));
    QCOMPARE(decoded.testFilePath, QStringLiteral("/tmp/fall-demo.mp4"));
    QCOMPARE(decoded.testPlaybackState, QStringLiteral("finished"));
}

void VideoProtocolTest::roundTripsStartTestInputCommandPayload() {
    VideoCommand command;
    command.action = QStringLiteral("start_test_input");
    command.requestId = QStringLiteral("video-2");
    command.cameraId = QStringLiteral("front_cam");
    command.payload.insert(QStringLiteral("file_path"), QStringLiteral("/tmp/fall-demo.mp4"));

    const QJsonObject json = videoCommandToJson(command);
    VideoCommand decoded;
    QVERIFY(videoCommandFromJson(json, &decoded));
    QCOMPARE(decoded.action, QStringLiteral("start_test_input"));
    QCOMPARE(decoded.payload.value(QStringLiteral("file_path")).toString(),
        QStringLiteral("/tmp/fall-demo.mp4"));
}
```

- [ ] **Step 2: Run the protocol test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-test-video-input-mode-build --target video_protocol_test -j4
ctest --test-dir /tmp/rk_health_station-test-video-input-mode-build -R video_protocol_test --output-on-failure
```

Expected: FAIL because `VideoChannelStatus` does not yet have `inputMode`, `testFilePath`, or `testPlaybackState`.

- [ ] **Step 3: Add backend-owned test-mode fields to `VideoChannelStatus`**

```cpp
struct VideoChannelStatus {
    QString cameraId;
    QString displayName;
    QString devicePath;
    VideoCameraState cameraState = VideoCameraState::Unavailable;
    QString previewUrl;
    QString storageDir;
    QString lastSnapshotPath;
    QString currentRecordPath;
    QString lastError;
    bool recording = false;
    QString inputMode = QStringLiteral("camera");
    QString testFilePath;
    QString testPlaybackState = QStringLiteral("idle");
    VideoProfile previewProfile;
    VideoProfile snapshotProfile;
    VideoProfile recordProfile;
};
```

- [ ] **Step 4: Serialize and parse the new status fields**

```cpp
QJsonObject videoChannelStatusToJson(const VideoChannelStatus &status) {
    QJsonObject json;
    json.insert(QStringLiteral("camera_id"), status.cameraId);
    json.insert(QStringLiteral("display_name"), status.displayName);
    json.insert(QStringLiteral("device_path"), status.devicePath);
    json.insert(QStringLiteral("camera_state"), videoCameraStateToString(status.cameraState));
    json.insert(QStringLiteral("preview_url"), status.previewUrl);
    json.insert(QStringLiteral("storage_dir"), status.storageDir);
    json.insert(QStringLiteral("last_snapshot_path"), status.lastSnapshotPath);
    json.insert(QStringLiteral("current_record_path"), status.currentRecordPath);
    json.insert(QStringLiteral("last_error"), status.lastError);
    json.insert(QStringLiteral("recording"), status.recording);
    json.insert(QStringLiteral("input_mode"), status.inputMode);
    json.insert(QStringLiteral("test_file_path"), status.testFilePath);
    json.insert(QStringLiteral("test_playback_state"), status.testPlaybackState);
    json.insert(QStringLiteral("preview_profile"), buildProfileJson(status.previewProfile));
    json.insert(QStringLiteral("snapshot_profile"), buildProfileJson(status.snapshotProfile));
    json.insert(QStringLiteral("record_profile"), buildProfileJson(status.recordProfile));
    return json;
}
```

```cpp
parsed.inputMode = json.value(QStringLiteral("input_mode")).toString(QStringLiteral("camera"));
parsed.testFilePath = json.value(QStringLiteral("test_file_path")).toString();
parsed.testPlaybackState = json.value(QStringLiteral("test_playback_state")).toString(QStringLiteral("idle"));
```

- [ ] **Step 5: Extend the UI video client contract with test-mode actions**

```cpp
class AbstractVideoClient : public QObject {
    Q_OBJECT
public:
    virtual bool connectToBackend() = 0;
    virtual void requestStatus(const QString &cameraId) = 0;
    virtual void takeSnapshot(const QString &cameraId) = 0;
    virtual void startRecording(const QString &cameraId) = 0;
    virtual void stopRecording(const QString &cameraId) = 0;
    virtual void setStorageDir(const QString &cameraId, const QString &storageDir) = 0;
    virtual void startTestInput(const QString &cameraId, const QString &filePath) = 0;
    virtual void stopTestInput(const QString &cameraId) = 0;
};
```

```cpp
void VideoIpcClient::startTestInput(const QString &cameraId, const QString &filePath) {
    QJsonObject payload;
    payload.insert(QStringLiteral("file_path"), filePath);
    sendCommand(QStringLiteral("start_test_input"), cameraId, payload);
}

void VideoIpcClient::stopTestInput(const QString &cameraId) {
    sendCommand(QStringLiteral("stop_test_input"), cameraId);
}
```

- [ ] **Step 6: Re-run the protocol test and a quick UI-client build**

Run:

```bash
cmake --build /tmp/rk_health_station-test-video-input-mode-build --target video_protocol_test health-ui -j4
ctest --test-dir /tmp/rk_health_station-test-video-input-mode-build -R video_protocol_test --output-on-failure
```

Expected: PASS. `health-ui` should compile with the new client API.

- [ ] **Step 7: Commit Task 1**

```bash
git add \
  rk_app/src/shared/models/video_models.h \
  rk_app/src/shared/protocol/video_ipc.cpp \
  rk_app/src/health_ui/ipc_client/video_ipc_client.h \
  rk_app/src/health_ui/ipc_client/video_ipc_client.cpp \
  rk_app/src/tests/protocol_tests/video_protocol_test.cpp

git commit -m "feat: add video test-mode status and client actions"
```

---

### Task 2: Add test-mode state switching in `health-videod`

**Files:**
- Modify: `rk_app/src/health_videod/core/video_service.h`
- Modify: `rk_app/src/health_videod/core/video_service.cpp`
- Modify: `rk_app/src/health_videod/ipc/video_gateway.cpp`
- Modify: `rk_app/src/health_videod/pipeline/video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Test: `rk_app/src/tests/video_daemon_tests/video_service_test.cpp`
- Test: `rk_app/src/tests/ipc_tests/video_gateway_test.cpp`

- [ ] **Step 1: Write the failing service tests for entering, finishing, and exiting test mode**

```cpp
void VideoServiceTest::startsTestInputAndDisablesCameraOnlyOperations() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);
    const QString path = QDir::temp().filePath(QStringLiteral("fall-demo.mp4"));
    QFile(path).open(QIODevice::WriteOnly);

    const VideoCommandResult result = service.startTestInput(QStringLiteral("front_cam"), path);
    QVERIFY(result.ok);

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.inputMode, QStringLiteral("test_file"));
    QCOMPARE(status.testFilePath, path);
    QCOMPARE(status.testPlaybackState, QStringLiteral("playing"));
    QCOMPARE(service.takeSnapshot(QStringLiteral("front_cam")).errorCode,
        QStringLiteral("unsupported_in_test_mode"));
    QCOMPARE(service.startRecording(QStringLiteral("front_cam")).errorCode,
        QStringLiteral("unsupported_in_test_mode"));
}

void VideoServiceTest::keepsTestModeAfterPlaybackFinished() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);
    const QString path = QDir::temp().filePath(QStringLiteral("fall-demo.mp4"));
    QFile(path).open(QIODevice::WriteOnly);

    QVERIFY(service.startTestInput(QStringLiteral("front_cam"), path).ok);
    backend.emitPlaybackFinished(QStringLiteral("front_cam"));

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.inputMode, QStringLiteral("test_file"));
    QCOMPARE(status.testPlaybackState, QStringLiteral("finished"));
}

void VideoServiceTest::restoresCameraModeWhenStoppingTestInput() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);
    const QString path = QDir::temp().filePath(QStringLiteral("fall-demo.mp4"));
    QFile(path).open(QIODevice::WriteOnly);

    QVERIFY(service.startTestInput(QStringLiteral("front_cam"), path).ok);
    const VideoCommandResult result = service.stopTestInput(QStringLiteral("front_cam"));
    QVERIFY(result.ok);

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.inputMode, QStringLiteral("camera"));
    QCOMPARE(status.testFilePath, QString());
    QCOMPARE(status.testPlaybackState, QStringLiteral("idle"));
}
```

- [ ] **Step 2: Run the service test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-test-video-input-mode-build --target video_service_test -j4
ctest --test-dir /tmp/rk_health_station-test-video-input-mode-build -R video_service_test --output-on-failure
```

Expected: FAIL because `VideoService` and `FakeVideoPipelineBackend` do not yet support test-mode entry, finish notifications, or exit.

- [ ] **Step 3: Add a tiny observer contract to the video pipeline interface**

```cpp
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
    virtual bool startPreview(const VideoChannelStatus &status, QString *previewUrl, QString *error) = 0;
    virtual bool stopPreview(const QString &cameraId, QString *error) = 0;
    virtual bool captureSnapshot(const VideoChannelStatus &status, const QString &outputPath, QString *error) = 0;
    virtual bool startRecording(const VideoChannelStatus &status, const QString &outputPath, QString *error) = 0;
    virtual bool stopRecording(const QString &cameraId, QString *error) = 0;
};
```

- [ ] **Step 4: Make `VideoService` own test-mode state transitions and observer callbacks**

```cpp
class VideoService : public QObject, private VideoPipelineObserver {
    Q_OBJECT
public:
    VideoCommandResult startTestInput(const QString &cameraId, const QString &filePath);
    VideoCommandResult stopTestInput(const QString &cameraId);

private:
    bool validateTestFilePath(const QString &filePath, QString *errorCode) const;
    bool restartPreviewForChannel(const QString &cameraId, QString *errorCode);
    void resetTestModeState(VideoChannelStatus *channel) const;
    void onPipelinePlaybackFinished(const QString &cameraId) override;
    void onPipelineRuntimeError(const QString &cameraId, const QString &error) override;
};
```

```cpp
VideoCommandResult VideoService::startTestInput(const QString &cameraId, const QString &filePath) {
    if (!channels_.contains(cameraId)) {
        return buildErrorResult(cameraId, QStringLiteral("start_test_input"),
            QStringLiteral("camera_not_found"));
    }
    QString validationError;
    if (!validateTestFilePath(filePath, &validationError)) {
        return buildErrorResult(cameraId, QStringLiteral("start_test_input"), validationError);
    }

    VideoChannelStatus next = channels_.value(cameraId);
    next.inputMode = QStringLiteral("test_file");
    next.testFilePath = QFileInfo(filePath).absoluteFilePath();
    next.testPlaybackState = QStringLiteral("playing");
    next.recording = false;
    next.currentRecordPath.clear();
    next.lastError.clear();
    channels_[cameraId] = next;

    QString previewError;
    if (!restartPreviewForChannel(cameraId, &previewError)) {
        return buildErrorResult(cameraId, QStringLiteral("start_test_input"), previewError);
    }
    return buildOkResult(cameraId, QStringLiteral("start_test_input"),
        videoChannelStatusToJson(channels_.value(cameraId)));
}
```

```cpp
VideoCommandResult VideoService::takeSnapshot(const QString &cameraId) {
    if (channels_.value(cameraId).inputMode == QStringLiteral("test_file")) {
        return buildErrorResult(cameraId, QStringLiteral("take_snapshot"),
            QStringLiteral("unsupported_in_test_mode"));
    }
    // existing camera snapshot path remains unchanged
}
```

- [ ] **Step 5: Route the new IPC actions in `VideoGateway`**

```cpp
if (command.action == QStringLiteral("start_test_input")) {
    return service_->startTestInput(
        command.cameraId, command.payload.value(QStringLiteral("file_path")).toString());
}
if (command.action == QStringLiteral("stop_test_input")) {
    return service_->stopTestInput(command.cameraId);
}
```

- [ ] **Step 6: Add MP4 preview support and finish notifications in the GStreamer backend**

```cpp
void GstreamerVideoPipelineBackend::setObserver(VideoPipelineObserver *observer) {
    observer_ = observer;
}

QString GstreamerVideoPipelineBackend::buildPreviewCommand(const VideoChannelStatus &status) const {
    if (status.inputMode == QStringLiteral("test_file")) {
        return QStringLiteral(
            "%1 -e filesrc location=%2 ! qtdemux ! decodebin ! videoconvert ! videoscale ! "
            "video/x-raw,width=%3,height=%4 ! jpegenc ! multipartmux boundary=%5 ! "
            "tcpserversink host=127.0.0.1 port=%6")
            .arg(shellQuote(gstLaunchBinary()))
            .arg(shellQuote(status.testFilePath))
            .arg(status.previewProfile.width)
            .arg(status.previewProfile.height)
            .arg(previewBoundaryForCamera(status.cameraId))
            .arg(previewPortForCamera(status.cameraId));
    }

    return QStringLiteral(
        "%1 -e v4l2src device=%2 ! "
        "video/x-raw,format=%3,width=%4,height=%5,framerate=%6/1 ! "
        "jpegenc ! multipartmux boundary=%7 ! tcpserversink host=127.0.0.1 port=%8")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.previewProfile.pixelFormat)
        .arg(status.previewProfile.width)
        .arg(status.previewProfile.height)
        .arg(status.previewProfile.fps > 0 ? status.previewProfile.fps : 30)
        .arg(previewBoundaryForCamera(status.cameraId))
        .arg(previewPortForCamera(status.cameraId));
}
```

```cpp
connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
    [this, cameraId, isTestInput = (status.inputMode == QStringLiteral("test_file"))]
    (int exitCode, QProcess::ExitStatus exitStatus) {
        if (!observer_) {
            return;
        }
        if (isTestInput && exitStatus == QProcess::NormalExit && exitCode == 0) {
            observer_->onPipelinePlaybackFinished(cameraId);
            return;
        }
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            observer_->onPipelineRuntimeError(cameraId, QStringLiteral("preview_pipeline_failed"));
        }
    });
```

- [ ] **Step 7: Add the gateway smoke test for `start_test_input`**

```cpp
void VideoGatewayTest::routesStartTestInputCommand() {
    qputenv("RK_VIDEO_SOCKET_NAME", QByteArray("/tmp/rk_video_gateway_test.sock"));

    VideoService service;
    VideoGateway gateway(&service);
    QVERIFY(gateway.start());

    const QString path = QDir::temp().filePath(QStringLiteral("fall-demo.mp4"));
    QFile(path).open(QIODevice::WriteOnly);

    QLocalSocket socket;
    socket.connectToServer(VideoGateway::socketName());
    QVERIFY(socket.waitForConnected(1000));

    VideoCommand command;
    command.action = QStringLiteral("start_test_input");
    command.requestId = QStringLiteral("video-2");
    command.cameraId = QStringLiteral("front_cam");
    command.payload.insert(QStringLiteral("file_path"), path);

    socket.write(QJsonDocument(videoCommandToJson(command)).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 1000);

    VideoCommandResult result;
    QVERIFY(videoCommandResultFromJson(
        QJsonDocument::fromJson(socket.readAll().trimmed()).object(), &result));
    QVERIFY(result.ok);
    QCOMPARE(result.action, QStringLiteral("start_test_input"));
}
```

- [ ] **Step 8: Re-run daemon-side tests**

Run:

```bash
cmake --build /tmp/rk_health_station-test-video-input-mode-build --target video_service_test video_gateway_test health-videod -j4
ctest --test-dir /tmp/rk_health_station-test-video-input-mode-build -R 'video_service_test|video_gateway_test' --output-on-failure
```

Expected: PASS.

- [ ] **Step 9: Commit Task 2**

```bash
git add \
  rk_app/src/health_videod/core/video_service.h \
  rk_app/src/health_videod/core/video_service.cpp \
  rk_app/src/health_videod/ipc/video_gateway.cpp \
  rk_app/src/health_videod/pipeline/video_pipeline_backend.h \
  rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h \
  rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp \
  rk_app/src/tests/video_daemon_tests/video_service_test.cpp \
  rk_app/src/tests/ipc_tests/video_gateway_test.cpp

git commit -m "feat: add backend test-video input mode"
```

---

### Task 3: Add Qt controls, mode labels, and preview badge

**Files:**
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.h`
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.cpp`
- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.h`
- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.cpp`
- Modify: `rk_app/src/health_ui/app/ui_app.cpp`
- Test: `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`
- Test: `rk_app/src/tests/ui_tests/video_preview_widget_test.cpp`

- [ ] **Step 1: Write the failing UI tests for mode rows, button gating, and source badge**

```cpp
void VideoMonitorPageTest::showsTestModeStatusAndDisablesCameraControls() {
    FakeVideoIpcClient client;
    FakeFallIpcClient fallClient;
    VideoMonitorPage page(&client, &fallClient, nullptr,
        []() { return QStringLiteral("/tmp/fall-demo.mp4"); });

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.inputMode = QStringLiteral("test_file");
    status.testFilePath = QStringLiteral("/tmp/fall-demo.mp4");
    status.testPlaybackState = QStringLiteral("finished");

    emit client.statusReceived(status);

    QCOMPARE(page.inputModeText(), QStringLiteral("Test Video (Finished)"));
    QCOMPARE(page.testFileText(), QStringLiteral("fall-demo.mp4"));
    QVERIFY(!page.takeSnapshotButton()->isEnabled());
    QVERIFY(!page.startRecordingButton()->isEnabled());
    QVERIFY(page.exitTestModeButton()->isEnabled());
}
```

```cpp
void VideoPreviewWidgetTest::showsIndependentSourceBadge() {
    VideoPreviewWidget widget;
    widget.setSourceBadge(QStringLiteral("TEST MODE"), QStringLiteral("fall-demo.mp4"));
    QCOMPARE(widget.sourceBadgeText(), QStringLiteral("TEST MODE\nfall-demo.mp4"));
}
```

- [ ] **Step 2: Run the UI tests to verify they fail**

Run:

```bash
cmake --build /tmp/rk_health_station-test-video-input-mode-build --target video_monitor_page_test video_preview_widget_test -j4
ctest --test-dir /tmp/rk_health_station-test-video-input-mode-build -R 'video_monitor_page_test|video_preview_widget_test' --output-on-failure
```

Expected: FAIL because the page and preview widget do not yet expose test-mode state or source badge APIs.

- [ ] **Step 3: Extend `VideoMonitorPage` with a file-picker callback, new labels, and test buttons**

```cpp
class VideoMonitorPage : public QWidget {
    Q_OBJECT
public:
    using TestVideoPicker = std::function<QString()>;

    explicit VideoMonitorPage(
        AbstractVideoClient *client,
        AbstractFallClient *fallClient,
        QWidget *parent = nullptr,
        TestVideoPicker pickTestVideo = TestVideoPicker());

    QString inputModeText() const;
    QString testFileText() const;
    QPushButton *takeSnapshotButton() const;
    QPushButton *exitTestModeButton() const;

private:
    QLabel *inputModeValue_ = nullptr;
    QLabel *testFileValue_ = nullptr;
    QPushButton *selectTestVideoButton_ = nullptr;
    QPushButton *exitTestModeButton_ = nullptr;
    TestVideoPicker pickTestVideo_;
};
```

```cpp
connect(selectTestVideoButton_, &QPushButton::clicked, this, [this]() {
    const QString path = pickTestVideo_ ? pickTestVideo_() : QString();
    if (!path.isEmpty()) {
        client_->startTestInput(currentCameraId_, path);
    }
});
connect(exitTestModeButton_, &QPushButton::clicked, this, [this]() {
    client_->stopTestInput(currentCameraId_);
});
```

- [ ] **Step 4: Render backend-owned mode/file state and gate buttons from `VideoChannelStatus`**

```cpp
void VideoMonitorPage::onStatusReceived(const VideoChannelStatus &status) {
    currentCameraId_ = status.cameraId;
    cameraStateValue_->setText(cameraStateLabel(status.cameraState));
    storageDirValue_->setText(status.storageDir);
    lastSnapshotValue_->setText(status.lastSnapshotPath.isEmpty() ? QStringLiteral("--") : status.lastSnapshotPath);
    currentRecordValue_->setText(status.currentRecordPath.isEmpty() ? QStringLiteral("--") : status.currentRecordPath);
    lastErrorValue_->setText(status.lastError.isEmpty() ? QStringLiteral("--") : status.lastError);

    const bool testMode = status.inputMode == QStringLiteral("test_file");
    inputModeValue_->setText(testMode
        ? (status.testPlaybackState == QStringLiteral("finished")
            ? QStringLiteral("Test Video (Finished)")
            : QStringLiteral("Test Video"))
        : QStringLiteral("Camera"));
    testFileValue_->setText(status.testFilePath.isEmpty()
        ? QStringLiteral("--")
        : QFileInfo(status.testFilePath).fileName());

    previewWidget_->setSourceBadge(
        testMode ? QStringLiteral("TEST MODE") : QString(),
        testMode ? QFileInfo(status.testFilePath).fileName() : QString());

    refreshButtonState(status);
}
```

```cpp
void VideoMonitorPage::refreshButtonState(const VideoChannelStatus &status) {
    const bool testMode = status.inputMode == QStringLiteral("test_file");
    const bool isRecording = status.cameraState == VideoCameraState::Recording || status.recording;

    takeSnapshotButton_->setEnabled(!testMode);
    startRecordingButton_->setEnabled(!testMode && !isRecording);
    stopRecordingButton_->setEnabled(!testMode && isRecording);
    exitTestModeButton_->setEnabled(testMode);
}
```

- [ ] **Step 5: Add a dedicated source badge API to the preview widget**

```cpp
class VideoPreviewWidget : public QWidget {
    Q_OBJECT
public:
    void setSourceBadge(const QString &title, const QString &subtitle = QString());
    QString sourceBadgeText() const;

private:
    QLabel *sourceBadgeLabel_ = nullptr;
    void updateSourceBadgeGeometry();
};
```

```cpp
void VideoPreviewWidget::setSourceBadge(const QString &title, const QString &subtitle) {
    const QString text = subtitle.isEmpty() ? title : QStringLiteral("%1\n%2").arg(title, subtitle);
    sourceBadgeLabel_->setText(text);
    sourceBadgeLabel_->setVisible(!text.isEmpty());
    sourceBadgeLabel_->adjustSize();
    updateSourceBadgeGeometry();
}
```

- [ ] **Step 6: Wire the default file chooser in `UiApp`**

```cpp
videoMonitorPage_(new VideoMonitorPage(
    videoClient_,
    fallClient_,
    stack_,
    [this]() {
        return QFileDialog::getOpenFileName(
            window_,
            QStringLiteral("Select Test Video"),
            QString(),
            QStringLiteral("Video Files (*.mp4 *.MP4);;All Files (*)"));
    })) {
```

- [ ] **Step 7: Re-run the UI tests and build the app**

Run:

```bash
cmake --build /tmp/rk_health_station-test-video-input-mode-build --target video_monitor_page_test video_preview_widget_test health-ui -j4
ctest --test-dir /tmp/rk_health_station-test-video-input-mode-build -R 'video_monitor_page_test|video_preview_widget_test' --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit Task 3**

```bash
git add \
  rk_app/src/health_ui/pages/video_monitor_page.h \
  rk_app/src/health_ui/pages/video_monitor_page.cpp \
  rk_app/src/health_ui/widgets/video_preview_widget.h \
  rk_app/src/health_ui/widgets/video_preview_widget.cpp \
  rk_app/src/health_ui/app/ui_app.cpp \
  rk_app/src/tests/ui_tests/video_monitor_page_test.cpp \
  rk_app/src/tests/ui_tests/video_preview_widget_test.cpp

git commit -m "feat: add UI controls for video test mode"
```

---

### Task 4: Run regression sweep and RK3588-ready verification

**Files:**
- Modify if needed: `rk_app/src/tests/video_daemon_tests/video_service_test.cpp`
- Modify if needed: `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`
- Modify if needed: `rk_app/src/tests/ui_tests/video_preview_widget_test.cpp`
- Modify if needed: `rk_app/src/tests/ipc_tests/video_gateway_test.cpp`

- [ ] **Step 1: Run the focused regression suite for protocol, video daemon, and UI**

Run:

```bash
cmake --build /tmp/rk_health_station-test-video-input-mode-build \
  --target video_protocol_test video_service_test video_gateway_test video_monitor_page_test video_preview_widget_test health-videod health-ui health-falld -j4

ctest --test-dir /tmp/rk_health_station-test-video-input-mode-build \
  -R 'video_protocol_test|video_service_test|video_gateway_test|video_monitor_page_test|video_preview_widget_test|fall_end_to_end_status_test' \
  --output-on-failure
```

Expected: PASS.

- [ ] **Step 2: Re-run the existing fall pipeline smoke test to confirm no regression in `health-falld` coupling**

Run:

```bash
cmake --build /tmp/rk_health_station-test-video-input-mode-build --target fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-test-video-input-mode-build -R fall_end_to_end_status_test --output-on-failure
```

Expected: PASS, proving the new source-mode feature did not require fall-daemon changes.

- [ ] **Step 3: Build the RK3588 bundle from this worktree**

Run:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/.worktrees/feature-test-video-input-mode
BUILD_DIR=/tmp/rk_health_station-test-video-input-mode-rk3588-build bash deploy/scripts/build_rk3588_bundle.sh
```

Expected: PASS with refreshed `out/rk3588_bundle/` artifacts.

- [ ] **Step 4: Optional board-ready manual verification checklist**

Use this after code lands, not before tests:

```bash
rsync -az out/rk3588_bundle/ elf@192.168.137.179:/home/elf/rk3588_bundle/
ssh elf@192.168.137.179 'cd /home/elf/rk3588_bundle && RK_RUNTIME_MODE=system ./scripts/start.sh --backend-only && ./scripts/status.sh'
```

Manual verification target:

- open the Video page
- click `Select Test Video`
- choose an MP4 with a fall event
- confirm preview switches from camera to MP4
- confirm `TEST MODE` badge + filename appear
- confirm `fall` / `lie` / `stand` overlay still updates
- confirm controls for snapshot / recording are disabled
- let the video finish and confirm the UI remains in test mode
- click `Exit Test Mode` and confirm live camera preview returns

- [ ] **Step 5: Commit final polish if any regression fixes were needed**

```bash
git add -A
git commit -m "test: finalize test video input mode regressions"
```

Use this step only if Task 4 required additional code changes. If no code changed, skip this commit.

---

## Self-Review Checklist

- Spec coverage:
  - UI controls and file selection: Task 3
  - backend source switching: Task 2
  - test-mode state in IPC/status: Task 1
  - stay in test mode after EOF: Task 2 + Task 3
  - disable snapshot/recording in test mode: Task 2 + Task 3
  - preserve fall-daemon decoupling: Task 4 regression check
- Placeholder scan: no unfinished placeholder markers remain in this plan.
- Type consistency:
  - status fields use `QString inputMode`, `QString testFilePath`, `QString testPlaybackState`
  - UI client methods use `startTestInput()` / `stopTestInput()`
  - daemon actions use `start_test_input` / `stop_test_input`
  - service observer methods use `onPipelinePlaybackFinished()` / `onPipelineRuntimeError()`

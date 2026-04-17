# RK3588 Fall Classification Realtime Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `health-falld` so each valid 45-frame classification window emits a realtime `stand / fall / lie` result over `rk_fall.sock`, writes matching structured logs, and preserves the existing runtime-status and fall-event paths.

**Architecture:** Keep the existing `health-videod -> analysis socket -> health-falld` flow unchanged. Add a dedicated classification message model in `rk_shared`, teach `FallGateway` to manage subscriber sockets and broadcast classification/event messages, and have `FallDaemonApp` publish each successful LSTM window result through that gateway while continuing to update runtime status.

**Tech Stack:** C++17, Qt Core/Network/Test, CMake/CTest, QLocalSocket, RKNN-backed LSTM runtime, bash, ssh.

---

## File Structure Map

### Existing files to modify

- `src/shared/models/fall_models.h`
  - add a dedicated `FallClassificationResult` model that is independent from runtime status and event payloads
- `src/shared/protocol/fall_ipc.h`
- `src/shared/protocol/fall_ipc.cpp`
  - add JSON encode/decode helpers for classification messages
- `src/health_falld/domain/fall_detector_service.h`
- `src/health_falld/domain/fall_detector_service.cpp`
  - return explicit classification fields plus optional `FallEvent`
- `src/health_falld/ipc/fall_gateway.h`
- `src/health_falld/ipc/fall_gateway.cpp`
  - handle `subscribe_classification`, track subscribers, and broadcast messages
- `src/health_falld/app/fall_daemon_app.cpp`
  - publish classification results, write logs, and broadcast optional events
- `src/tests/protocol_tests/fall_protocol_test.cpp`
  - cover classification JSON round-trip
- `src/tests/fall_daemon_tests/fall_gateway_test.cpp`
  - cover subscription and broadcast behavior
- `src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
  - add realtime subscription validation without regressing runtime status
- `src/tests/CMakeLists.txt`
  - register any new fall-daemon test target only if required

### Existing files expected to remain unchanged

- `src/health_videod/**`
  - no analysis stream producer changes
- `src/health_falld/action/**`
  - classifier interface and LSTM backend stay focused on sequence -> label/confidence only
- `src/shared/protocol/analysis_stream_protocol.*`
  - no upstream frame protocol changes

## Notes Before Implementation

- This tree does not currently have `.git`, so each commit step below is only a checkpoint command for future use; do not block on commit execution.
- Follow TDD strictly: write the failing test first, watch it fail, then implement the minimum code to make it pass.
- Realtime output means every valid classification window produces a pushed `classification` message. Do not add state-change suppression.
- `monitoring` remains a runtime-status value for invalid/unready phases; do not broadcast it as a formal classification message.

### Task 1: Add a formal classification message model and JSON codec

**Files:**
- Modify: `src/shared/models/fall_models.h`
- Modify: `src/shared/protocol/fall_ipc.h`
- Modify: `src/shared/protocol/fall_ipc.cpp`
- Modify: `src/tests/protocol_tests/fall_protocol_test.cpp`

- [ ] **Step 1: Write the failing protocol test**

Add a second test to `src/tests/protocol_tests/fall_protocol_test.cpp`:

```cpp
void FallProtocolTest::roundTripsClassificationJson() {
    FallClassificationResult result;
    result.cameraId = QStringLiteral("front_cam");
    result.timestampMs = 1776356876397;
    result.state = QStringLiteral("fall");
    result.confidence = 0.93;

    const QJsonObject json = fallClassificationResultToJson(result);
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification"));

    FallClassificationResult decoded;
    QVERIFY(fallClassificationResultFromJson(json, &decoded));
    QCOMPARE(decoded.cameraId, result.cameraId);
    QCOMPARE(decoded.timestampMs, result.timestampMs);
    QCOMPARE(decoded.state, result.state);
    QCOMPARE(decoded.confidence, result.confidence);
}
```

Declare it in the `private slots:` section.

- [ ] **Step 2: Run the protocol test to verify it fails**

Run:

```bash
cmake -S /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/rk_app -B /tmp/rk_health_station-realtime
cmake --build /tmp/rk_health_station-realtime --target fall_protocol_test -j4
ctest --test-dir /tmp/rk_health_station-realtime -R fall_protocol_test --output-on-failure
```

Expected: FAIL because `FallClassificationResult`, `fallClassificationResultToJson`, and `fallClassificationResultFromJson` do not exist yet.

- [ ] **Step 3: Add the minimal classification model and codec**

Add this model to `src/shared/models/fall_models.h` near `FallEvent`:

```cpp
struct FallClassificationResult {
    QString cameraId;
    qint64 timestampMs = 0;
    QString state;
    double confidence = 0.0;
};
```

Add these declarations to `src/shared/protocol/fall_ipc.h`:

```cpp
QJsonObject fallClassificationResultToJson(const FallClassificationResult &result);
bool fallClassificationResultFromJson(const QJsonObject &json, FallClassificationResult *result);
```

Add this implementation to `src/shared/protocol/fall_ipc.cpp` above `fallEventToJson`:

```cpp
QJsonObject fallClassificationResultToJson(const FallClassificationResult &result) {
    QJsonObject json;
    json.insert(QStringLiteral("type"), QStringLiteral("classification"));
    json.insert(QStringLiteral("camera_id"), result.cameraId);
    json.insert(QStringLiteral("ts"), static_cast<qint64>(result.timestampMs));
    json.insert(QStringLiteral("state"), result.state);
    json.insert(QStringLiteral("confidence"), result.confidence);
    return json;
}

bool fallClassificationResultFromJson(const QJsonObject &json, FallClassificationResult *result) {
    if (!result) {
        return false;
    }

    result->cameraId = json.value(QStringLiteral("camera_id")).toString();
    result->timestampMs = static_cast<qint64>(json.value(QStringLiteral("ts")).toDouble());
    result->state = json.value(QStringLiteral("state")).toString();
    result->confidence = json.value(QStringLiteral("confidence")).toDouble();
    return json.value(QStringLiteral("type")).toString() == QStringLiteral("classification")
        && !result->cameraId.isEmpty()
        && !result->state.isEmpty();
}
```

- [ ] **Step 4: Re-run the protocol test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-realtime --target fall_protocol_test -j4
ctest --test-dir /tmp/rk_health_station-realtime -R fall_protocol_test --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/shared/models/fall_models.h src/shared/protocol/fall_ipc.h src/shared/protocol/fall_ipc.cpp src/tests/protocol_tests/fall_protocol_test.cpp
git commit -m "feat: add fall classification protocol model"
```

### Task 2: Teach `FallGateway` to keep subscribers and broadcast classification results

**Files:**
- Modify: `src/health_falld/ipc/fall_gateway.h`
- Modify: `src/health_falld/ipc/fall_gateway.cpp`
- Modify: `src/tests/fall_daemon_tests/fall_gateway_test.cpp`

- [ ] **Step 1: Write the failing gateway subscription test**

Extend `src/tests/fall_daemon_tests/fall_gateway_test.cpp` with a new test:

```cpp
void FallGatewayTest::pushesClassificationToSubscribers() {
    const QString socketName = QStringLiteral("/tmp/rk_fall_subscribe_test.sock");
    QLocalServer::removeServer(socketName);

    FallGateway gateway(FallRuntimeStatus());
    gateway.setSocketName(socketName);
    QVERIFY(gateway.start());

    QLocalSocket subscriber;
    subscriber.connectToServer(socketName);
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();

    FallClassificationResult result;
    result.cameraId = QStringLiteral("front_cam");
    result.timestampMs = 1776356876397;
    result.state = QStringLiteral("fall");
    result.confidence = 0.93;
    gateway.publishClassification(result);

    QTRY_VERIFY_WITH_TIMEOUT(subscriber.bytesAvailable() > 0 || subscriber.waitForReadyRead(50), 2000);
    const QJsonObject json = QJsonDocument::fromJson(subscriber.readAll().trimmed()).object();
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification"));
    QCOMPARE(json.value(QStringLiteral("state")).toString(), QStringLiteral("fall"));

    gateway.stop();
}
```

Also add the slot declaration and required `#include <QJsonDocument>` / `#include <QLocalServer>`.

- [ ] **Step 2: Run the gateway test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-realtime --target fall_gateway_test -j4
ctest --test-dir /tmp/rk_health_station-realtime -R fall_gateway_test --output-on-failure
```

Expected: FAIL because `publishClassification` and subscriber management do not exist.

- [ ] **Step 3: Add the minimal subscription and broadcast logic**

Add these declarations to `src/health_falld/ipc/fall_gateway.h`:

```cpp
#include <QPointer>
#include <QVector>

public:
    void publishClassification(const FallClassificationResult &result);
    void publishEvent(const FallEvent &event);

private:
    void removeSubscriber(QLocalSocket *socket);
    QByteArray buildClassificationMessage(const FallClassificationResult &result) const;
    QByteArray buildEventMessage(const FallEvent &event) const;

    QVector<QPointer<QLocalSocket>> classificationSubscribers_;
```

Update `onNewConnection()` in `src/health_falld/ipc/fall_gateway.cpp` so every socket also connects its `disconnected` signal to a lambda that calls `removeSubscriber(socket)` before `deleteLater()`.

Update `onSocketReadyRead()` with the new branch:

```cpp
if (request.contains("subscribe_classification")) {
    if (!classificationSubscribers_.contains(socket)) {
        classificationSubscribers_.push_back(socket);
    }
    return;
}
```

Add a helper for line-delimited JSON messages:

```cpp
QByteArray FallGateway::buildClassificationMessage(const FallClassificationResult &result) const {
    QByteArray payload = QJsonDocument(fallClassificationResultToJson(result)).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}
```

Add `publishClassification(...)` to iterate over subscribers, skip null or disconnected sockets, write the message, and flush.

Add `buildEventMessage(...)` and `publishEvent(...)` in the same style, wrapping `fallEventToJson(event)` and inserting `type=fall_event` before serialization.

- [ ] **Step 4: Re-run the gateway test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-realtime --target fall_gateway_test -j4
ctest --test-dir /tmp/rk_health_station-realtime -R fall_gateway_test --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/ipc/fall_gateway.h src/health_falld/ipc/fall_gateway.cpp src/tests/fall_daemon_tests/fall_gateway_test.cpp
git commit -m "feat: add realtime fall classification subscriptions"
```

### Task 3: Return explicit classification results from the detector service and publish them from `FallDaemonApp`

**Files:**
- Modify: `src/health_falld/domain/fall_detector_service.h`
- Modify: `src/health_falld/domain/fall_detector_service.cpp`
- Modify: `src/health_falld/app/fall_daemon_app.cpp`
- Modify: `src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`

- [ ] **Step 1: Write the failing end-to-end subscription test**

Add a new test to `src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`:

```cpp
void FallEndToEndStatusTest::streamsClassificationAfterWindowFills() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e_stream.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e_stream.sock")));

    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_stream.sock"));
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e_stream.sock"));
    qputenv("RK_FALL_SEQUENCE_LENGTH", QByteArray("1"));

    FallDaemonApp app;
    QVERIFY(app.start());
    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    QLocalSocket subscriber;
    subscriber.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_stream.sock"));
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();

    AnalysisFramePacket packet;
    packet.frameId = 99;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 640;
    packet.payload = QByteArray("jpeg-bytes");
    analysisSocket->write(encodeAnalysisFramePacket(packet));
    analysisSocket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(subscriber.bytesAvailable() > 0 || subscriber.waitForReadyRead(50), 2000);
    const QJsonObject json = QJsonDocument::fromJson(subscriber.readAll().trimmed()).object();
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification"));
    QVERIFY(!json.value(QStringLiteral("state")).toString().isEmpty());

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
    qunsetenv("RK_FALL_SEQUENCE_LENGTH");
}
```

Declare the slot and add any missing includes.

- [ ] **Step 2: Run the end-to-end test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-realtime --target fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-realtime -R fall_end_to_end_status_test --output-on-failure
```

Expected: FAIL because `FallDaemonApp` does not publish classification messages yet.

- [ ] **Step 3: Add explicit classification fields to `FallDetectorResult`**

Update `src/health_falld/domain/fall_detector_service.h`:

```cpp
struct FallDetectorResult {
    QString state = QStringLiteral("monitoring");
    double confidence = 0.0;
    bool hasClassification = false;
    QString classificationState;
    double classificationConfidence = 0.0;
    std::optional<FallEvent> event;
};
```

Update `src/health_falld/domain/fall_detector_service.cpp` so a successful classifier output sets:

```cpp
result.state = classification.label;
result.confidence = classification.confidence;
result.hasClassification = true;
result.classificationState = classification.label;
result.classificationConfidence = classification.confidence;
```

- [ ] **Step 4: Publish classification and event output in `FallDaemonApp`**

In `src/health_falld/app/fall_daemon_app.cpp`, inside the successful branch after `detectorService_.update(...)`:

```cpp
runtimeStatus_.lastError.clear();
runtimeStatus_.latestState = result.state;
runtimeStatus_.latestConfidence = result.confidence;

if (result.hasClassification) {
    FallClassificationResult classification;
    classification.cameraId = config_.cameraId;
    classification.timestampMs = runtimeStatus_.lastInferTs;
    classification.state = result.classificationState;
    classification.confidence = result.classificationConfidence;
    gateway_->publishClassification(classification);
    qInfo().noquote() << QStringLiteral("classification camera=%1 state=%2 confidence=%3 ts=%4")
        .arg(classification.cameraId)
        .arg(classification.state)
        .arg(QString::number(classification.confidence, 'f', 3))
        .arg(classification.timestampMs);
}

if (result.event.has_value()) {
    FallEvent event = *result.event;
    event.cameraId = config_.cameraId;
    event.tsConfirm = runtimeStatus_.lastInferTs;
    gateway_->publishEvent(event);
    qInfo().noquote() << QStringLiteral("fall_event camera=%1 type=%2 confidence=%3 ts=%4")
        .arg(event.cameraId)
        .arg(event.eventType)
        .arg(QString::number(event.confidence, 'f', 3))
        .arg(event.tsConfirm);
}
```

Also add `#include <QDebug>` near the top if needed.

- [ ] **Step 5: Re-run the end-to-end test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-realtime --target fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-realtime -R fall_end_to_end_status_test --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Checkpoint**

```bash
git add src/health_falld/domain/fall_detector_service.h src/health_falld/domain/fall_detector_service.cpp src/health_falld/app/fall_daemon_app.cpp src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
git commit -m "feat: publish realtime fall classification results"
```

### Task 4: Run the focused regression set, rebuild the RK3588 bundle, and verify realtime output on the board

**Files:**
- No additional code changes required unless verification fails
- Runtime evidence to collect:
  - `~/rk3588_bundle/logs/health-falld.log`
  - subscriber output from `~/rk3588_bundle/run/rk_fall.sock`

- [ ] **Step 1: Run the focused host-side regression set**

Run:

```bash
cmake --build /tmp/rk_health_station-realtime --target fall_protocol_test fall_gateway_test fall_end_to_end_status_test action_classifier_factory_test -j4
ctest --test-dir /tmp/rk_health_station-realtime -R 'fall_protocol_test|fall_gateway_test|fall_end_to_end_status_test|action_classifier_factory_test' --output-on-failure
```

Expected: PASS with zero failures.

- [ ] **Step 2: Rebuild the RK3588 bundle with the verified host changes**

Run:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station
bash deploy/scripts/build_rk3588_bundle.sh
```

Expected: bundle rebuild succeeds and `out/rk3588_bundle/bin/health-falld` remains `ARM aarch64`.

- [ ] **Step 3: Sync the bundle to the board and restart backend-only mode**

Run:

```bash
rsync -av /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/ elf@192.168.137.179:~/rk3588_bundle/
ssh elf@192.168.137.179 'cd ~/rk3588_bundle && RK_RUNTIME_MODE=system ./scripts/stop.sh || true && RK_RUNTIME_MODE=system ./scripts/start.sh --backend-only'
```

Expected: `healthd`, `health-videod`, and `health-falld` all start.

- [ ] **Step 4: Subscribe to realtime classification output on the board**

Run:

```bash
ssh elf@192.168.137.179 'cd ~/rk3588_bundle && python3 - <<"PY"
import socket
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
client.connect("./run/rk_fall.sock")
client.sendall(b"{\"action\":\"subscribe_classification\"}\n")
for _ in range(5):
    data = client.recv(4096)
    if not data:
        break
    print(data.decode().strip())
client.close()
PY'
```

Expected: one or more line-delimited JSON `classification` messages appear while frames are flowing.

- [ ] **Step 5: Verify the board log shows the same realtime classification records**

Run:

```bash
ssh elf@192.168.137.179 'cd ~/rk3588_bundle && tail -n 80 logs/health-falld.log'
```

Expected: lines matching `classification camera=... state=... confidence=... ts=...` appear, and no `GLIBC_`, `rknn_init_failed`, or `action_model_not_loaded` errors appear.

- [ ] **Step 6: If verification fails, stop and debug before any further feature work**

Use this mapping:

```text
No subscriber output but runtime status works -> gateway subscription/broadcast bug
Subscriber output exists but logs missing -> app logging path bug
Logs exist but subscriber output missing -> socket subscription cleanup or write path bug
Both missing but runtime_status updates -> detector-to-publish bridge bug
```

Only patch the smallest layer responsible, then re-run Steps 1-5.

- [ ] **Step 7: Checkpoint**

```bash
git add src/health_falld src/shared src/tests deploy
git commit -m "test: validate realtime fall classification output on rk3588"
```


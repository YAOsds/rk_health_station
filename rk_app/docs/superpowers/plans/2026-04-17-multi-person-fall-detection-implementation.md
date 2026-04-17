# Multi-Person Fall Detection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the current single-target fall pipeline into a max-5-person runtime that tracks multiple people independently, classifies each person with an independent 45-frame LSTM sequence, and renders multi-line status output in `health-ui`.

**Architecture:** Keep `health-videod` and the analysis socket unchanged. Add a lightweight multi-person tracking layer inside `health-falld`, publish batch classification snapshots over `rk_fall.sock`, and let `health-ui` consume those snapshots and render a top-left list overlay without exposing runtime `track_id` to users.

**Tech Stack:** C++17, Qt Core/Network/Widgets/Test, existing RKNN pose + LSTM runtime, CMake/CTest, RK3588 bundle deployment.

---

## File Structure Map

### New files

- `rk_app/src/health_falld/tracking/tracked_person.h`
  - per-track runtime state: `trackId`, bbox/pose cache, miss count, independent sequence buffer, latest classification summary
- `rk_app/src/health_falld/tracking/track_manager.h`
- `rk_app/src/health_falld/tracking/track_manager.cpp`
  - max-5 multi-person association and lifecycle management using IoU + center distance
- `rk_app/src/tests/fall_daemon_tests/track_manager_test.cpp`
  - focused tests for create/match/evict semantics

### Existing files to modify

- `rk_app/src/shared/models/fall_models.h`
  - add `FallClassificationEntry` and `FallClassificationBatch`
- `rk_app/src/shared/protocol/fall_ipc.h`
- `rk_app/src/shared/protocol/fall_ipc.cpp`
  - add batch JSON encode/decode helpers
- `rk_app/src/health_falld/CMakeLists.txt`
  - compile tracking files
- `rk_app/src/health_falld/ipc/fall_gateway.h`
- `rk_app/src/health_falld/ipc/fall_gateway.cpp`
  - broadcast batch messages to subscribers
- `rk_app/src/health_falld/app/fall_daemon_app.h`
- `rk_app/src/health_falld/app/fall_daemon_app.cpp`
  - replace single `TargetSelector + SequenceBuffer` flow with multi-track update loop
- `rk_app/src/tests/fall_daemon_tests/fall_gateway_test.cpp`
  - verify batch broadcasting
- `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
  - verify multiple tracked people can produce batch output
- `rk_app/src/health_ui/ipc_client/fall_ipc_client.h`
- `rk_app/src/health_ui/ipc_client/fall_ipc_client.cpp`
  - parse `classification_batch`
- `rk_app/src/health_ui/widgets/video_preview_widget.h`
- `rk_app/src/health_ui/widgets/video_preview_widget.cpp`
  - replace single overlay label API with multi-line overlay list rendering
- `rk_app/src/health_ui/pages/video_monitor_page.h`
- `rk_app/src/health_ui/pages/video_monitor_page.cpp`
  - consume batch snapshots and map them into ordered overlay rows
- `rk_app/src/tests/ui_tests/fall_ipc_client_test.cpp`
  - add batch subscription test
- `rk_app/src/tests/ui_tests/video_preview_widget_test.cpp`
  - add multi-row overlay rendering tests
- `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`
  - add batch-to-overlay mapping tests
- `rk_app/src/tests/CMakeLists.txt`
  - register the new `track_manager_test` and ensure other targets link new files where needed
- `rk_app/src/health_ui/CMakeLists.txt`
  - no new files expected, but confirm modified sources are compiled cleanly

### Existing files expected to remain unchanged

- `rk_app/src/health_videod/**`
  - no video daemon protocol changes
- `rk_app/src/shared/protocol/analysis_stream_protocol.*`
  - no analysis socket contract change
- `rk_app/src/health_falld/action/rknn_lstm_action_classifier.*`
  - model input remains per-sequence, unchanged shape

## Notes Before Implementation

- Follow strict TDD: every new behavior starts with a failing test.
- Keep `track_id` runtime-internal; UI must not display it.
- Do not introduce ReID, face ID, or cross-session identity semantics.
- Cap active tracks at 5.
- Preserve the existing single-service boundaries: tracking stays in `health-falld`, rendering stays in `health-ui`.

### Task 1: Add shared multi-person batch models and protocol codec

**Files:**
- Modify: `rk_app/src/shared/models/fall_models.h`
- Modify: `rk_app/src/shared/protocol/fall_ipc.h`
- Modify: `rk_app/src/shared/protocol/fall_ipc.cpp`
- Modify: `rk_app/src/tests/protocol_tests/fall_protocol_test.cpp`

- [ ] **Step 1: Write the failing protocol test for batch JSON round-trip**

Update `rk_app/src/tests/protocol_tests/fall_protocol_test.cpp` by adding this test case:

```cpp
void FallProtocolTest::roundTripsClassificationBatch() {
    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1776367000000;

    FallClassificationEntry first;
    first.state = QStringLiteral("stand");
    first.confidence = 0.91;
    batch.results.push_back(first);

    FallClassificationEntry second;
    second.state = QStringLiteral("fall");
    second.confidence = 0.96;
    batch.results.push_back(second);

    QJsonObject json = fallClassificationBatchToJson(batch);
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification_batch"));
    QCOMPARE(json.value(QStringLiteral("person_count")).toInt(), 2);

    FallClassificationBatch decoded;
    QVERIFY(fallClassificationBatchFromJson(json, &decoded));
    QCOMPARE(decoded.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(decoded.results.size(), 2);
    QCOMPARE(decoded.results.at(0).state, QStringLiteral("stand"));
    QCOMPARE(decoded.results.at(1).state, QStringLiteral("fall"));
    QCOMPARE(decoded.results.at(1).confidence, 0.96);
}
```

- [ ] **Step 2: Run the protocol test to verify it fails**

Run:

```bash
cmake -S /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/rk_app -B /tmp/rk_health_station-build
cmake --build /tmp/rk_health_station-build --target fall_protocol_test -j4
ctest --test-dir /tmp/rk_health_station-build -R fall_protocol_test --output-on-failure
```

Expected: FAIL because `FallClassificationBatch`, `FallClassificationEntry`, and batch codec helpers do not exist yet.

- [ ] **Step 3: Implement the shared batch models**

In `rk_app/src/shared/models/fall_models.h`, add these structures near `FallClassificationResult`:

```cpp
struct FallClassificationEntry {
    QString state;
    double confidence = 0.0;
};

struct FallClassificationBatch {
    QString cameraId;
    qint64 timestampMs = 0;
    QVector<FallClassificationEntry> results;
};

Q_DECLARE_METATYPE(FallClassificationBatch)
```

Also add `#include <QVector>` at the top if it is not already present.

- [ ] **Step 4: Implement batch JSON helpers**

In `rk_app/src/shared/protocol/fall_ipc.h`, add:

```cpp
QJsonObject fallClassificationBatchToJson(const FallClassificationBatch &batch);
bool fallClassificationBatchFromJson(const QJsonObject &json, FallClassificationBatch *batch);
```

In `rk_app/src/shared/protocol/fall_ipc.cpp`, add:

```cpp
QJsonObject fallClassificationBatchToJson(const FallClassificationBatch &batch) {
    QJsonArray results;
    for (const FallClassificationEntry &entry : batch.results) {
        QJsonObject item;
        item.insert(QStringLiteral("state"), entry.state);
        item.insert(QStringLiteral("confidence"), entry.confidence);
        results.append(item);
    }

    QJsonObject json;
    json.insert(QStringLiteral("type"), QStringLiteral("classification_batch"));
    json.insert(QStringLiteral("camera_id"), batch.cameraId);
    json.insert(QStringLiteral("ts"), static_cast<qint64>(batch.timestampMs));
    json.insert(QStringLiteral("person_count"), batch.results.size());
    json.insert(QStringLiteral("results"), results);
    return json;
}

bool fallClassificationBatchFromJson(const QJsonObject &json, FallClassificationBatch *batch) {
    if (!batch) {
        return false;
    }

    batch->cameraId = json.value(QStringLiteral("camera_id")).toString();
    batch->timestampMs = static_cast<qint64>(json.value(QStringLiteral("ts")).toDouble());
    batch->results.clear();

    const QJsonArray results = json.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject object = value.toObject();
        FallClassificationEntry entry;
        entry.state = object.value(QStringLiteral("state")).toString();
        entry.confidence = object.value(QStringLiteral("confidence")).toDouble();
        if (entry.state.isEmpty()) {
            return false;
        }
        batch->results.push_back(entry);
    }

    return json.value(QStringLiteral("type")).toString() == QStringLiteral("classification_batch")
        && !batch->cameraId.isEmpty()
        && json.value(QStringLiteral("person_count")).toInt() == batch->results.size();
}
```

- [ ] **Step 5: Re-run the protocol test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_protocol_test -j4
ctest --test-dir /tmp/rk_health_station-build -R fall_protocol_test --output-on-failure
```

Expected: PASS with the new batch round-trip test green.

- [ ] **Step 6: Commit**

```bash
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station add \
  rk_app/src/shared/models/fall_models.h \
  rk_app/src/shared/protocol/fall_ipc.h \
  rk_app/src/shared/protocol/fall_ipc.cpp \
  rk_app/src/tests/protocol_tests/fall_protocol_test.cpp
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station commit -m "feat: add multi-person fall batch protocol"
```

### Task 2: Add lightweight multi-person track management

**Files:**
- Create: `rk_app/src/health_falld/tracking/tracked_person.h`
- Create: `rk_app/src/health_falld/tracking/track_manager.h`
- Create: `rk_app/src/health_falld/tracking/track_manager.cpp`
- Modify: `rk_app/src/health_falld/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Create: `rk_app/src/tests/fall_daemon_tests/track_manager_test.cpp`

- [ ] **Step 1: Write the failing track manager tests**

Create `rk_app/src/tests/fall_daemon_tests/track_manager_test.cpp`:

```cpp
#include "tracking/track_manager.h"

#include <QtTest/QTest>

namespace {
PosePerson makePerson(float left, float top, float width, float height, float score = 0.9f) {
    PosePerson person;
    person.box = QRectF(left, top, width, height);
    person.score = score;
    person.keypoints.fill(PoseKeypoint(), 17);
    return person;
}
}

class TrackManagerTest : public QObject {
    Q_OBJECT

private slots:
    void assignsTwoStableTracks();
    void dropsTrackAfterMissThreshold();
    void limitsActiveTracksToFive();
};

void TrackManagerTest::assignsTwoStableTracks() {
    TrackManager manager(5, 10);

    const auto firstFrame = manager.update({
        makePerson(10, 10, 40, 80),
        makePerson(200, 10, 40, 80)
    }, 1000);
    QCOMPARE(firstFrame.size(), 2);

    const int leftId = firstFrame.at(0).trackId;
    const int rightId = firstFrame.at(1).trackId;

    const auto secondFrame = manager.update({
        makePerson(14, 12, 40, 80),
        makePerson(204, 12, 40, 80)
    }, 1033);
    QCOMPARE(secondFrame.size(), 2);
    QCOMPARE(secondFrame.at(0).trackId, leftId);
    QCOMPARE(secondFrame.at(1).trackId, rightId);
}

void TrackManagerTest::dropsTrackAfterMissThreshold() {
    TrackManager manager(5, 2);
    QCOMPARE(manager.update({makePerson(10, 10, 40, 80)}, 1000).size(), 1);
    QCOMPARE(manager.update({}, 1033).size(), 1);
    QCOMPARE(manager.update({}, 1066).size(), 1);
    QCOMPARE(manager.update({}, 1099).size(), 0);
}

void TrackManagerTest::limitsActiveTracksToFive() {
    TrackManager manager(5, 10);
    QVector<PosePerson> people;
    for (int i = 0; i < 7; ++i) {
        people.push_back(makePerson(float(i * 60), 10, 40, 80, 0.95f - 0.01f * i));
    }

    const auto tracks = manager.update(people, 1000);
    QCOMPARE(tracks.size(), 5);
}

QTEST_MAIN(TrackManagerTest)
#include "track_manager_test.moc"
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Update `rk_app/src/tests/CMakeLists.txt` with a new target:

```cmake
add_executable(track_manager_test
    fall_daemon_tests/track_manager_test.cpp
    ../health_falld/tracking/track_manager.cpp
)

set_target_properties(track_manager_test PROPERTIES AUTOMOC ON)

target_include_directories(track_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_falld
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(track_manager_test PRIVATE
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)

add_test(NAME track_manager_test COMMAND track_manager_test)
```

Run:

```bash
cmake --build /tmp/rk_health_station-build --target track_manager_test -j4
ctest --test-dir /tmp/rk_health_station-build -R track_manager_test --output-on-failure
```

Expected: FAIL because `TrackManager` does not exist yet.

- [ ] **Step 3: Add the tracked person runtime model**

Create `rk_app/src/health_falld/tracking/tracked_person.h`:

```cpp
#pragma once

#include "action/sequence_buffer.h"
#include "pose/pose_types.h"

#include <QString>

struct TrackedPerson {
    int trackId = -1;
    PosePerson latestPose;
    qint64 lastUpdateTs = 0;
    int missCount = 0;
    SequenceBuffer<PosePerson> sequence;
    QString lastClassificationState = QStringLiteral("monitoring");
    double lastClassificationConfidence = 0.0;
    bool hasFreshClassification = false;

    explicit TrackedPerson(int sequenceLength = 45)
        : sequence(sequenceLength) {
    }
};
```

- [ ] **Step 4: Implement the minimal `TrackManager`**

Create `rk_app/src/health_falld/tracking/track_manager.h`:

```cpp
#pragma once

#include "tracking/tracked_person.h"

#include <QVector>

class TrackManager {
public:
    TrackManager(int maxTracks = 5, int maxMissedFrames = 10);

    QVector<TrackedPerson> update(const QVector<PosePerson> &detections, qint64 timestampMs);
    QVector<TrackedPerson> activeTracks() const;
    void clear();

private:
    int findBestTrackIndex(const PosePerson &detection, QVector<bool> *matched) const;
    static double iou(const QRectF &left, const QRectF &right);
    static double centerDistanceSquared(const QRectF &left, const QRectF &right);

    int maxTracks_ = 5;
    int maxMissedFrames_ = 10;
    int nextTrackId_ = 1;
    QVector<TrackedPerson> tracks_;
};
```

Create `rk_app/src/health_falld/tracking/track_manager.cpp` with the minimal matching behavior:

```cpp
#include "tracking/track_manager.h"

#include <QtMath>
#include <algorithm>

TrackManager::TrackManager(int maxTracks, int maxMissedFrames)
    : maxTracks_(maxTracks)
    , maxMissedFrames_(maxMissedFrames) {
}

QVector<TrackedPerson> TrackManager::update(const QVector<PosePerson> &detections, qint64 timestampMs) {
    QVector<bool> matched(tracks_.size(), false);

    for (const PosePerson &detection : detections) {
        if (detection.keypoints.size() < 17 || !detection.box.isValid()) {
            continue;
        }

        const int bestIndex = findBestTrackIndex(detection, &matched);
        if (bestIndex >= 0) {
            TrackedPerson &track = tracks_[bestIndex];
            track.latestPose = detection;
            track.lastUpdateTs = timestampMs;
            track.missCount = 0;
            matched[bestIndex] = true;
            continue;
        }

        if (tracks_.size() >= maxTracks_) {
            continue;
        }

        TrackedPerson track(45);
        track.trackId = nextTrackId_++;
        track.latestPose = detection;
        track.lastUpdateTs = timestampMs;
        tracks_.push_back(track);
        matched.push_back(true);
    }

    for (int i = 0; i < tracks_.size(); ++i) {
        if (!matched.value(i, false)) {
            tracks_[i].missCount += 1;
        }
    }

    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [this](const TrackedPerson &track) {
        return track.missCount > maxMissedFrames_;
    }), tracks_.end());

    std::sort(tracks_.begin(), tracks_.end(), [](const TrackedPerson &left, const TrackedPerson &right) {
        return left.latestPose.box.center().x() < right.latestPose.box.center().x();
    });

    return tracks_;
}

QVector<TrackedPerson> TrackManager::activeTracks() const {
    return tracks_;
}

void TrackManager::clear() {
    tracks_.clear();
}

double TrackManager::iou(const QRectF &left, const QRectF &right) {
    const QRectF intersection = left.intersected(right);
    if (intersection.isEmpty()) {
        return 0.0;
    }
    const double intersectionArea = intersection.width() * intersection.height();
    const double unionArea = left.width() * left.height() + right.width() * right.height() - intersectionArea;
    return unionArea <= 0.0 ? 0.0 : intersectionArea / unionArea;
}

double TrackManager::centerDistanceSquared(const QRectF &left, const QRectF &right) {
    const QPointF delta = left.center() - right.center();
    return delta.x() * delta.x() + delta.y() * delta.y();
}

int TrackManager::findBestTrackIndex(const PosePerson &detection, QVector<bool> *matched) const {
    int bestIndex = -1;
    double bestScore = -1.0;
    for (int i = 0; i < tracks_.size(); ++i) {
        if (matched && matched->value(i, false)) {
            continue;
        }
        const double overlap = iou(tracks_.at(i).latestPose.box, detection.box);
        const double distance = centerDistanceSquared(tracks_.at(i).latestPose.box, detection.box);
        if (overlap < 0.1 && distance > 2500.0) {
            continue;
        }
        const double score = overlap - distance / 100000.0;
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }
    return bestIndex;
}
```

- [ ] **Step 5: Wire the new source files into the fall daemon build**

In `rk_app/src/health_falld/CMakeLists.txt`, add:

```cmake
    tracking/track_manager.cpp
    tracking/track_manager.h
    tracking/tracked_person.h
```

near the other `health-falld` sources.

- [ ] **Step 6: Run the new track test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target track_manager_test -j4
ctest --test-dir /tmp/rk_health_station-build -R track_manager_test --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station add \
  rk_app/src/health_falld/tracking/tracked_person.h \
  rk_app/src/health_falld/tracking/track_manager.h \
  rk_app/src/health_falld/tracking/track_manager.cpp \
  rk_app/src/health_falld/CMakeLists.txt \
  rk_app/src/tests/CMakeLists.txt \
  rk_app/src/tests/fall_daemon_tests/track_manager_test.cpp
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station commit -m "feat: add lightweight multi-person tracking"
```

### Task 3: Integrate multi-track classification into `health-falld` and publish batch output

**Files:**
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.h`
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.cpp`
- Modify: `rk_app/src/health_falld/ipc/fall_gateway.h`
- Modify: `rk_app/src/health_falld/ipc/fall_gateway.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_gateway_test.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`

- [ ] **Step 1: Add the failing gateway test for batch broadcast**

In `rk_app/src/tests/fall_daemon_tests/fall_gateway_test.cpp`, add:

```cpp
void FallGatewayTest::broadcastsClassificationBatchToSubscribers() {
    FallGateway gateway(FallRuntimeStatus());
    gateway.setSocketName(QStringLiteral("/tmp/rk_fall_gateway_batch_test.sock"));
    QVERIFY(gateway.start());

    QLocalSocket subscriber;
    subscriber.connectToServer(QStringLiteral("/tmp/rk_fall_gateway_batch_test.sock"));
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();
    QTest::qWait(50);

    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1776367000000;
    batch.results.push_back({QStringLiteral("stand"), 0.91});
    batch.results.push_back({QStringLiteral("fall"), 0.96});

    gateway.publishClassificationBatch(batch);

    QTRY_VERIFY_WITH_TIMEOUT(subscriber.bytesAvailable() > 0 || subscriber.waitForReadyRead(50), 2000);
    const QJsonObject json = QJsonDocument::fromJson(subscriber.readAll().trimmed()).object();
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification_batch"));
    QCOMPARE(json.value(QStringLiteral("person_count")).toInt(), 2);
}
```

- [ ] **Step 2: Run the gateway test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_gateway_test -j4
ctest --test-dir /tmp/rk_health_station-build -R fall_gateway_test --output-on-failure
```

Expected: FAIL because `publishClassificationBatch()` does not exist.

- [ ] **Step 3: Extend the gateway for batch publishing**

In `rk_app/src/health_falld/ipc/fall_gateway.h`, add:

```cpp
void publishClassificationBatch(const FallClassificationBatch &batch);
QByteArray buildClassificationBatchMessage(const FallClassificationBatch &batch) const;
```

In `rk_app/src/health_falld/ipc/fall_gateway.cpp`, add:

```cpp
void FallGateway::publishClassificationBatch(const FallClassificationBatch &batch) {
    const QByteArray message = buildClassificationBatchMessage(batch);
    for (int index = classificationSubscribers_.size() - 1; index >= 0; --index) {
        QLocalSocket *socket = classificationSubscribers_.at(index).data();
        if (!socket || socket->state() != QLocalSocket::ConnectedState) {
            classificationSubscribers_.removeAt(index);
            continue;
        }
        socket->write(message);
        socket->flush();
    }
}

QByteArray FallGateway::buildClassificationBatchMessage(const FallClassificationBatch &batch) const {
    QByteArray payload = QJsonDocument(fallClassificationBatchToJson(batch)).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}
```

- [ ] **Step 4: Add the failing end-to-end multi-person test**

In `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`, add:

```cpp
void FallEndToEndStatusTest::streamsClassificationBatchForMultiplePeople() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e_multi.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e_multi.sock")));

    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_multi.sock"));
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e_multi.sock"));

    FallDaemonApp app(std::make_unique<MultiPoseEstimatorStub>());
    QVERIFY(app.start());
    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    QLocalSocket subscriber;
    subscriber.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_multi.sock"));
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();
    QTest::qWait(50);

    for (quint64 frameId = 1; frameId <= 45; ++frameId) {
        AnalysisFramePacket packet;
        packet.frameId = frameId;
        packet.cameraId = QStringLiteral("front_cam");
        packet.width = 640;
        packet.height = 640;
        packet.payload = QByteArray("jpeg-bytes");
        analysisSocket->write(encodeAnalysisFramePacket(packet));
        analysisSocket->flush();
    }

    QTRY_VERIFY_WITH_TIMEOUT(subscriber.bytesAvailable() > 0 || subscriber.waitForReadyRead(50), 2000);
    const QJsonObject json = QJsonDocument::fromJson(subscriber.readAll().trimmed()).object();
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification_batch"));
    QVERIFY(json.value(QStringLiteral("person_count")).toInt() >= 2);

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}
```

Use a local `MultiPoseEstimatorStub` in that test file that returns two stable `PosePerson` objects with 17 keypoints for every frame.

- [ ] **Step 5: Run the end-to-end test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-build -R fall_end_to_end_status_test --output-on-failure
```

Expected: FAIL because `FallDaemonApp` still publishes only single-target results.

- [ ] **Step 6: Replace single-target sequence handling with per-track update logic**

In `rk_app/src/health_falld/app/fall_daemon_app.h`, replace the single buffer + selector members:

```cpp
    std::unique_ptr<TargetSelector> targetSelector_;
    SequenceBuffer<PosePerson> sequenceBuffer_;
```

with:

```cpp
    TrackManager trackManager_;
```

and add `#include "tracking/track_manager.h"`.

In `rk_app/src/health_falld/app/fall_daemon_app.cpp`, replace the `people.isEmpty() -> selectPrimary() -> sequenceBuffer_` block with a per-track loop:

```cpp
            const QVector<TrackedPerson> tracks = trackManager_.update(people, runtimeStatus_.lastInferTs);
            if (tracks.isEmpty()) {
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                gateway_->setRuntimeStatus(runtimeStatus_);
                return;
            }

            FallClassificationBatch batch;
            batch.cameraId = config_.cameraId;
            batch.timestampMs = runtimeStatus_.lastInferTs;

            double highestConfidence = 0.0;
            QString highestState = QStringLiteral("monitoring");

            for (TrackedPerson track : tracks) {
                if (track.latestPose.keypoints.size() < 17) {
                    continue;
                }

                TrackedPerson mutableTrack = track;
                mutableTrack.sequence.push(mutableTrack.latestPose);
                if (!mutableTrack.sequence.isFull()) {
                    continue;
                }

                QString classifyError;
                const FallDetectorResult result = detectorService_.update(mutableTrack.sequence.values(), &classifyError);
                if (!classifyError.isEmpty() || !result.hasClassification) {
                    continue;
                }

                FallClassificationEntry entry;
                entry.state = result.classificationState;
                entry.confidence = result.classificationConfidence;
                batch.results.push_back(entry);

                if (entry.confidence >= highestConfidence) {
                    highestConfidence = entry.confidence;
                    highestState = entry.state;
                }
            }

            runtimeStatus_.latestState = batch.results.isEmpty() ? QStringLiteral("monitoring") : highestState;
            runtimeStatus_.latestConfidence = highestConfidence;
            if (!batch.results.isEmpty()) {
                gateway_->publishClassificationBatch(batch);
                QStringList states;
                for (const FallClassificationEntry &entry : batch.results) {
                    states.push_back(QStringLiteral("%1:%2")
                        .arg(entry.state)
                        .arg(QString::number(entry.confidence, 'f', 2)));
                }
                qInfo().noquote()
                    << QStringLiteral("classification_batch camera=%1 count=%2 states=[%3] ts=%4")
                           .arg(batch.cameraId)
                           .arg(batch.results.size())
                           .arg(states.join(','))
                           .arg(batch.timestampMs);
            }
```

Important when implementing for real:
- Do not copy `TrackedPerson` by value if sequence updates need to persist; update the actual stored track object in `TrackManager`.
- Expose a mutable-track update API if necessary instead of mutating a temporary copy.

- [ ] **Step 7: Re-run the backend tests to verify they pass**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_gateway_test fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_gateway_test|fall_end_to_end_status_test' --output-on-failure
```

Expected: PASS with batch broadcasting and multi-person end-to-end output.

- [ ] **Step 8: Commit**

```bash
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station add \
  rk_app/src/health_falld/app/fall_daemon_app.h \
  rk_app/src/health_falld/app/fall_daemon_app.cpp \
  rk_app/src/health_falld/ipc/fall_gateway.h \
  rk_app/src/health_falld/ipc/fall_gateway.cpp \
  rk_app/src/tests/fall_daemon_tests/fall_gateway_test.cpp \
  rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station commit -m "feat: publish multi-person fall classification batches"
```

### Task 4: Update `health-ui` to consume batch snapshots and render multi-line overlay

**Files:**
- Modify: `rk_app/src/health_ui/ipc_client/fall_ipc_client.h`
- Modify: `rk_app/src/health_ui/ipc_client/fall_ipc_client.cpp`
- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.h`
- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.cpp`
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.h`
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.cpp`
- Modify: `rk_app/src/tests/ui_tests/fall_ipc_client_test.cpp`
- Modify: `rk_app/src/tests/ui_tests/video_preview_widget_test.cpp`
- Modify: `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`

- [ ] **Step 1: Add the failing UI-side IPC batch test**

In `rk_app/src/tests/ui_tests/fall_ipc_client_test.cpp`, add:

```cpp
void FallIpcClientTest::receivesClassificationBatch() {
    const QString socketName = QStringLiteral("/tmp/rk_fall_ui_batch_test.sock");
    QLocalServer::removeServer(socketName);

    QLocalServer server;
    QVERIFY(server.listen(socketName));

    FallIpcClient client(socketName);
    QSignalSpy batchSpy(&client, SIGNAL(classificationBatchUpdated(FallClassificationBatch)));

    QVERIFY(client.connectToBackend());
    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(socket->bytesAvailable() > 0 || socket->waitForReadyRead(50), 2000);
    QVERIFY(socket->readAll().contains("subscribe_classification"));

    const QByteArray line = QByteArrayLiteral(
        "{\"type\":\"classification_batch\",\"camera_id\":\"front_cam\",\"ts\":1776367000000,\"person_count\":2,\"results\":[{\"state\":\"stand\",\"confidence\":0.91},{\"state\":\"fall\",\"confidence\":0.96}]}\n");
    socket->write(line);
    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(batchSpy.count(), 1, 2000);
    const FallClassificationBatch batch = batchSpy.takeFirst().at(0).value<FallClassificationBatch>();
    QCOMPARE(batch.results.size(), 2);
    QCOMPARE(batch.results.at(1).state, QStringLiteral("fall"));
}
```

- [ ] **Step 2: Run the IPC test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_ipc_client_test -j4
ctest --test-dir /tmp/rk_health_station-build -R fall_ipc_client_test --output-on-failure
```

Expected: FAIL because batch signals and parsing do not exist yet.

- [ ] **Step 3: Extend `FallIpcClient` for batch delivery**

In `rk_app/src/health_ui/ipc_client/fall_ipc_client.h`, add:

```cpp
signals:
    void classificationBatchUpdated(const FallClassificationBatch &batch);
```

In `rk_app/src/health_ui/ipc_client/fall_ipc_client.cpp`, update `onReadyRead()` to try batch parsing first:

```cpp
        FallClassificationBatch batch;
        if (document.isObject() && fallClassificationBatchFromJson(document.object(), &batch)) {
            emit classificationBatchUpdated(batch);
            continue;
        }
```

Retain single-result parsing only if needed for backward compatibility.

- [ ] **Step 4: Add the failing multi-row widget test**

In `rk_app/src/tests/ui_tests/video_preview_widget_test.cpp`, add:

```cpp
void VideoPreviewWidgetTest::rendersMultipleClassificationRows() {
    VideoPreviewWidget widget;
    widget.setClassificationRows({
        {QStringLiteral("stand 0.91"), VideoPreviewWidget::OverlaySeverity::Normal},
        {QStringLiteral("fall 0.96"), VideoPreviewWidget::OverlaySeverity::Alert},
        {QStringLiteral("lie 0.88"), VideoPreviewWidget::OverlaySeverity::Warning}
    });

    const QStringList rows = widget.classificationRows();
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.at(0), QStringLiteral("stand 0.91"));
    QCOMPARE(rows.at(1), QStringLiteral("fall 0.96"));
    QCOMPARE(rows.at(2), QStringLiteral("lie 0.88"));
}
```

- [ ] **Step 5: Run the widget test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target video_preview_widget_test -j4
ctest --test-dir /tmp/rk_health_station-build -R video_preview_widget_test --output-on-failure
```

Expected: FAIL because the widget only supports one overlay string.

- [ ] **Step 6: Upgrade the widget API to a row list**

In `rk_app/src/health_ui/widgets/video_preview_widget.h`, add:

```cpp
struct ClassificationOverlayRow {
    QString text;
    OverlaySeverity severity = OverlaySeverity::Muted;
};

void setClassificationRows(const QVector<ClassificationOverlayRow> &rows);
QStringList classificationRows() const;
```

and replace the single `classificationLabel_` member with:

```cpp
QVector<QLabel *> classificationLabels_;
```

In `rk_app/src/health_ui/widgets/video_preview_widget.cpp`, implement:

```cpp
void VideoPreviewWidget::setClassificationRows(const QVector<ClassificationOverlayRow> &rows) {
    while (classificationLabels_.size() < rows.size()) {
        auto *label = new QLabel(frameLabel_);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMargin(8);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        classificationLabels_.push_back(label);
    }

    for (int i = 0; i < classificationLabels_.size(); ++i) {
        QLabel *label = classificationLabels_.at(i);
        if (i >= rows.size()) {
            label->hide();
            label->clear();
            continue;
        }
        label->setText(rows.at(i).text);
        applyClassificationStyle(label, rows.at(i).severity);
        label->adjustSize();
        label->move(12, 12 + i * 44);
        label->show();
        label->raise();
    }
}

QStringList VideoPreviewWidget::classificationRows() const {
    QStringList rows;
    for (QLabel *label : classificationLabels_) {
        if (label && !label->text().isEmpty() && label->isVisible()) {
            rows.push_back(label->text());
        }
    }
    return rows;
}
```

Change `applyClassificationStyle()` to accept `QLabel *label` as an argument so each row can be styled independently.

- [ ] **Step 7: Add the failing page-level mapping test**

In `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`, add:

```cpp
void VideoMonitorPageTest::showsMultiPersonOverlayRows() {
    FakeVideoIpcClient client;
    FakeFallIpcClient fallClient;
    VideoMonitorPage page(&client, &fallClient);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    emit client.statusReceived(status);
    emit fallClient.connectionChanged(true);

    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1776367000000;
    batch.results.push_back({QStringLiteral("stand"), 0.91});
    batch.results.push_back({QStringLiteral("fall"), 0.96});

    emit fallClient.classificationBatchUpdated(batch);

    const QStringList rows = page.previewOverlayRows();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0), QStringLiteral("stand 0.91"));
    QCOMPARE(rows.at(1), QStringLiteral("fall 0.96"));
}
```

- [ ] **Step 8: Run the page test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target video_monitor_page_test -j4
ctest --test-dir /tmp/rk_health_station-build -R video_monitor_page_test --output-on-failure
```

Expected: FAIL because the page only handles single-result overlays.

- [ ] **Step 9: Map batch snapshots into ordered overlay rows**

In `rk_app/src/health_ui/pages/video_monitor_page.h`, add:

```cpp
QStringList previewOverlayRows() const;
void onClassificationBatchUpdated(const FallClassificationBatch &batch);
static QVector<VideoPreviewWidget::ClassificationOverlayRow> overlayRowsForBatch(
    const FallClassificationBatch &batch);
```

In `rk_app/src/health_ui/pages/video_monitor_page.cpp`, connect the new signal:

```cpp
        connect(fallClient_, &AbstractFallClient::classificationBatchUpdated,
            this, &VideoMonitorPage::onClassificationBatchUpdated);
```

and implement:

```cpp
void VideoMonitorPage::onClassificationBatchUpdated(const FallClassificationBatch &batch) {
    if (!batch.cameraId.isEmpty() && batch.cameraId != currentCameraId_) {
        return;
    }
    if (!previewAvailable_) {
        return;
    }
    if (batch.results.isEmpty()) {
        previewWidget_->setClassificationRows({{
            QStringLiteral("no person"),
            VideoPreviewWidget::OverlaySeverity::Muted
        }});
        return;
    }
    previewWidget_->setClassificationRows(overlayRowsForBatch(batch));
}

QVector<VideoPreviewWidget::ClassificationOverlayRow> VideoMonitorPage::overlayRowsForBatch(
    const FallClassificationBatch &batch) {
    QVector<VideoPreviewWidget::ClassificationOverlayRow> rows;
    for (const FallClassificationEntry &entry : batch.results) {
        VideoPreviewWidget::ClassificationOverlayRow row;
        row.text = QStringLiteral("%1 %2")
            .arg(entry.state)
            .arg(QString::number(entry.confidence, 'f', 2));
        row.severity = overlaySeverityForState(entry.state);
        rows.push_back(row);
    }
    return rows;
}

QStringList VideoMonitorPage::previewOverlayRows() const {
    return previewWidget_->classificationRows();
}
```

The existing single-result path can be removed once all tests are updated to batch semantics.

- [ ] **Step 10: Re-run the UI tests to verify they pass**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_ipc_client_test video_preview_widget_test video_monitor_page_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_ipc_client_test|video_preview_widget_test|video_monitor_page_test' --output-on-failure
```

Expected: PASS.

- [ ] **Step 11: Commit**

```bash
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station add \
  rk_app/src/health_ui/ipc_client/fall_ipc_client.h \
  rk_app/src/health_ui/ipc_client/fall_ipc_client.cpp \
  rk_app/src/health_ui/widgets/video_preview_widget.h \
  rk_app/src/health_ui/widgets/video_preview_widget.cpp \
  rk_app/src/health_ui/pages/video_monitor_page.h \
  rk_app/src/health_ui/pages/video_monitor_page.cpp \
  rk_app/src/tests/ui_tests/fall_ipc_client_test.cpp \
  rk_app/src/tests/ui_tests/video_preview_widget_test.cpp \
  rk_app/src/tests/ui_tests/video_monitor_page_test.cpp
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station commit -m "feat: show multi-person fall status overlay"
```

### Task 5: Full verification and RK3588 validation

**Files:**
- No new product files required unless board validation reveals a deployment issue
- Optional docs note if a runtime quirk is discovered: `RK3588_Qt_AI开发必读手册.md`

- [ ] **Step 1: Run the focused local regression suite**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target \
  fall_protocol_test \
  track_manager_test \
  fall_gateway_test \
  fall_end_to_end_status_test \
  fall_ipc_client_test \
  video_preview_widget_test \
  video_monitor_page_test \
  health-ui \
  health-falld -j4
ctest --test-dir /tmp/rk_health_station-build -R 'fall_protocol_test|track_manager_test|fall_gateway_test|fall_end_to_end_status_test|fall_ipc_client_test|video_preview_widget_test|video_monitor_page_test' --output-on-failure
```

Expected: all selected tests PASS.

- [ ] **Step 2: Rebuild the RK3588 bundle**

Run:

```bash
bash /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/deploy/scripts/build_rk3588_bundle.sh
```

Expected: `health-ui`, `health-falld`, `health-videod`, `healthd` all build as ARM aarch64 and the bundle is regenerated.

- [ ] **Step 3: Deploy the system-runtime-minimized bundle to the board**

Run the same minimized deployment flow already used in previous board validation:

```bash
# host side: prepare minimized system-runtime bundle copy
rm -rf /tmp/rk3588_bundle_system_min
mkdir -p /tmp/rk3588_bundle_system_min/{bin,scripts,assets,model,lib/app,logs,data,run}
cp -a /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/bin/. /tmp/rk3588_bundle_system_min/bin/
cp -a /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/scripts/. /tmp/rk3588_bundle_system_min/scripts/
cp -a /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/assets/. /tmp/rk3588_bundle_system_min/assets/
cp -a /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/model/. /tmp/rk3588_bundle_system_min/model/
cp -a /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/lib/app/. /tmp/rk3588_bundle_system_min/lib/app/
cp -a /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/bundle.env /tmp/rk3588_bundle_system_min/
rsync -av --delete /tmp/rk3588_bundle_system_min/ elf@192.168.137.179:/home/elf/rk3588_bundle_ui_overlay/
```

Expected: deployment succeeds without filling the board root filesystem.

- [ ] **Step 4: Start backend-only on the board and verify batch-ready runtime**

Run on the board:

```bash
cd /home/elf/rk3588_bundle_ui_overlay
RK_RUNTIME_MODE=system ./scripts/stop.sh >/dev/null 2>&1 || true
RK_RUNTIME_MODE=system ./scripts/start.sh --backend-only
RK_RUNTIME_MODE=system ./scripts/status.sh
```

Then query runtime status:

```bash
python3 - <<'PY'
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/home/elf/rk3588_bundle_ui_overlay/run/rk_fall.sock')
s.sendall(b'{"action":"get_runtime_status"}\n')
print(s.recv(4096).decode(), end='')
s.close()
PY
```

Expected: `pose_model_ready=true`, `action_model_ready=true`, `input_connected=true`.

- [ ] **Step 5: Verify batch output on the board with a multi-person test clip or camera scene**

Use either a live multi-person camera scene or a replay source that produces at least two visible people. Then subscribe to `rk_fall.sock`:

```bash
python3 - <<'PY'
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/home/elf/rk3588_bundle_ui_overlay/run/rk_fall.sock')
s.sendall(b'{"action":"subscribe_classification"}\n')
while True:
    data = s.recv(4096)
    if not data:
        break
    print(data.decode(), end='')
PY
```

Expected: lines with `"type":"classification_batch"` and `"person_count"` >= 2 when two people are visible.

- [ ] **Step 6: Verify UI multi-line overlay on the board**

Launch UI with correct absolute socket env vars:

```bash
cd /home/elf/rk3588_bundle_ui_overlay
export RK_HEALTH_STATION_SOCKET_NAME=$PWD/run/rk_health_station.sock
export RK_VIDEO_SOCKET_NAME=$PWD/run/rk_video.sock
export RK_FALL_SOCKET_NAME=$PWD/run/rk_fall.sock
./bin/health-ui --open-video-page
```

Expected in the real display session:
- preview opens
- multiple overlay rows appear when multiple people are present
- `fall` row is red and visually strongest
- no `person_id` appears in UI text

- [ ] **Step 7: Capture evidence and stop services cleanly**

Collect logs:

```bash
cd /home/elf/rk3588_bundle_ui_overlay
cat logs/health-falld.log | tail -n 80
./scripts/stop.sh
./scripts/status.sh
```

Expected log lines include batch summaries such as:

```text
classification_batch camera=front_cam count=2 states=[stand:0.91,fall:0.96] ts=...
```

- [ ] **Step 8: Commit any deployment/runtime fixes if needed**

Only if board validation required code or deployment changes, commit them with a focused message such as:

```bash
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station add <changed-files>
git -C /home/elf/workspace/QTtest/Qt例程源码/rk_health_station commit -m "fix: stabilize multi-person fall runtime on rk3588"
```

# Track Icon Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Visualize the existing per-track fall-detection correspondence by showing a stable small icon above each tracked person in the video preview and the same icon in the UI status list.

**Architecture:** Extend the existing `classification_batch` payload so `health-falld` publishes UI-facing overlay metadata (`trackId`, stable `iconId`, anchor, bbox) alongside `state` and `confidence`. Keep camera/video transport unchanged; `health-ui` consumes the enriched fall IPC message and renders the overlay in `VideoPreviewWidget` plus a synchronized icon in the status list.

**Tech Stack:** C++17, Qt5/Qt6 Widgets/Test, existing `rk_shared` protocol models, `health-falld`, `health-ui`, CMake/CTest.

---

## File Structure

### Shared protocol and model layer

- Modify: `rk_app/src/shared/models/fall_models.h`
  - Extend `FallClassificationEntry` with overlay metadata fields.
- Modify: `rk_app/src/shared/protocol/fall_ipc.h`
  - Keep function signatures stable unless compile pressure requires helper additions.
- Modify: `rk_app/src/shared/protocol/fall_ipc.cpp`
  - Encode/decode the new fields while preserving backward compatibility.
- Test: `rk_app/src/tests/protocol_tests/fall_protocol_test.cpp`
  - Add round-trip coverage for the new fields and old-message compatibility.

### Fall daemon runtime layer

- Create: `rk_app/src/health_falld/tracking/track_icon_registry.h`
  - Own stable `iconId` allocation per active `trackId`.
- Create: `rk_app/src/health_falld/tracking/track_icon_registry.cpp`
  - Implement allocation, release, and active-frame reconciliation.
- Modify: `rk_app/src/health_falld/CMakeLists.txt`
  - Build the new helper into `health-falld`.
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.h`
  - Add icon registry member and helper declarations if needed.
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.cpp`
  - Compute per-track anchor/bbox payload and publish enriched batch results.
- Test: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
  - Assert enriched `classification_batch` payload fields.
- Create: `rk_app/src/tests/fall_daemon_tests/track_icon_registry_test.cpp`
  - Unit-test stable `iconId` allocation and release behavior.

### UI IPC and page state layer

- Modify: `rk_app/src/health_ui/ipc_client/fall_ipc_client.h`
  - Expose enriched batch data to page consumers without changing transport behavior.
- Modify: `rk_app/src/health_ui/ipc_client/fall_ipc_client.cpp`
  - Decode the new payload and preserve old-message compatibility.
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.h`
  - Store overlay entries for both the list and preview widget.
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.cpp`
  - Build list rows with icons and push overlay entries into the preview widget.
- Test: `rk_app/src/tests/ui_tests/fall_ipc_client_test.cpp`
  - Verify enriched decode path.
- Test: `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`
  - Verify list order, icon text, and preview overlay propagation.

### Preview overlay rendering layer

- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.h`
  - Add overlay entry model and setter.
- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.cpp`
  - Paint icons at scaled anchor positions on top of the current preview frame.
- Test: `rk_app/src/tests/ui_tests/video_preview_widget_test.cpp`
  - Verify setter behavior and rendering-facing state transitions.

---

### Task 1: Extend the shared fall batch protocol with overlay metadata

**Files:**
- Modify: `rk_app/src/shared/models/fall_models.h`
- Modify: `rk_app/src/shared/protocol/fall_ipc.cpp`
- Test: `rk_app/src/tests/protocol_tests/fall_protocol_test.cpp`

- [ ] **Step 1: Write the failing protocol test**

Add this test case to `rk_app/src/tests/protocol_tests/fall_protocol_test.cpp`:

```cpp
void FallProtocolTest::roundTripsClassificationBatchOverlayMetadata() {
    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1777000000000;

    FallClassificationEntry first;
    first.trackId = 3;
    first.iconId = 1;
    first.state = QStringLiteral("stand");
    first.confidence = 0.98;
    first.anchorX = 312.0;
    first.anchorY = 118.0;
    first.bboxX = 250.0;
    first.bboxY = 90.0;
    first.bboxW = 120.0;
    first.bboxH = 260.0;
    batch.results.push_back(first);

    const QJsonObject json = fallClassificationBatchToJson(batch);
    const QJsonArray results = json.value(QStringLiteral("results")).toArray();
    QCOMPARE(results.size(), 1);

    const QJsonObject firstJson = results.first().toObject();
    QCOMPARE(firstJson.value(QStringLiteral("track_id")).toInt(), 3);
    QCOMPARE(firstJson.value(QStringLiteral("icon_id")).toInt(), 1);
    QCOMPARE(firstJson.value(QStringLiteral("anchor_x")).toDouble(), 312.0);
    QCOMPARE(firstJson.value(QStringLiteral("anchor_y")).toDouble(), 118.0);
    QCOMPARE(firstJson.value(QStringLiteral("bbox_x")).toDouble(), 250.0);
    QCOMPARE(firstJson.value(QStringLiteral("bbox_y")).toDouble(), 90.0);
    QCOMPARE(firstJson.value(QStringLiteral("bbox_w")).toDouble(), 120.0);
    QCOMPARE(firstJson.value(QStringLiteral("bbox_h")).toDouble(), 260.0);

    FallClassificationBatch decoded;
    QVERIFY(fallClassificationBatchFromJson(json, &decoded));
    QCOMPARE(decoded.results.size(), 1);
    QCOMPARE(decoded.results.first().trackId, 3);
    QCOMPARE(decoded.results.first().iconId, 1);
    QCOMPARE(decoded.results.first().anchorX, 312.0);
    QCOMPARE(decoded.results.first().anchorY, 118.0);
    QCOMPARE(decoded.results.first().bboxW, 120.0);
    QCOMPARE(decoded.results.first().bboxH, 260.0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake -S rk_app -B /tmp/rk_health_station-track-icon-overlay-host -DBUILD_TESTING=ON -DRKAPP_ENABLE_REAL_RKNN_POSE=OFF -DRKAPP_ENABLE_REAL_RKNN_ACTION=OFF
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target fall_protocol_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host -R fall_protocol_test --output-on-failure
```

Expected: FAIL with compiler errors that `FallClassificationEntry` does not contain `trackId`, `iconId`, `anchorX`, `anchorY`, `bboxX`, `bboxY`, `bboxW`, or `bboxH`.

- [ ] **Step 3: Write the minimal implementation**

Update `rk_app/src/shared/models/fall_models.h` so `FallClassificationEntry` becomes:

```cpp
struct FallClassificationEntry {
    int trackId = -1;
    int iconId = -1;
    QString state;
    double confidence = 0.0;
    double anchorX = 0.0;
    double anchorY = 0.0;
    double bboxX = 0.0;
    double bboxY = 0.0;
    double bboxW = 0.0;
    double bboxH = 0.0;
};
```

Update `rk_app/src/shared/protocol/fall_ipc.cpp` inside batch encode/decode so each entry writes and reads:

```cpp
entryJson.insert(QStringLiteral("track_id"), entry.trackId);
entryJson.insert(QStringLiteral("icon_id"), entry.iconId);
entryJson.insert(QStringLiteral("anchor_x"), entry.anchorX);
entryJson.insert(QStringLiteral("anchor_y"), entry.anchorY);
entryJson.insert(QStringLiteral("bbox_x"), entry.bboxX);
entryJson.insert(QStringLiteral("bbox_y"), entry.bboxY);
entryJson.insert(QStringLiteral("bbox_w"), entry.bboxW);
entryJson.insert(QStringLiteral("bbox_h"), entry.bboxH);
```

And on decode:

```cpp
entry.trackId = entryJson.value(QStringLiteral("track_id")).toInt(-1);
entry.iconId = entryJson.value(QStringLiteral("icon_id")).toInt(-1);
entry.anchorX = entryJson.value(QStringLiteral("anchor_x")).toDouble();
entry.anchorY = entryJson.value(QStringLiteral("anchor_y")).toDouble();
entry.bboxX = entryJson.value(QStringLiteral("bbox_x")).toDouble();
entry.bboxY = entryJson.value(QStringLiteral("bbox_y")).toDouble();
entry.bboxW = entryJson.value(QStringLiteral("bbox_w")).toDouble();
entry.bboxH = entryJson.value(QStringLiteral("bbox_h")).toDouble();
```

- [ ] **Step 4: Add backward-compatibility coverage**

Add this second test to `rk_app/src/tests/protocol_tests/fall_protocol_test.cpp`:

```cpp
void FallProtocolTest::decodesLegacyClassificationBatchWithoutOverlayMetadata() {
    const QJsonObject json {
        {QStringLiteral("type"), QStringLiteral("classification_batch")},
        {QStringLiteral("camera_id"), QStringLiteral("front_cam")},
        {QStringLiteral("ts"), 1777000000000LL},
        {QStringLiteral("results"), QJsonArray {
            QJsonObject {
                {QStringLiteral("state"), QStringLiteral("fall")},
                {QStringLiteral("confidence"), 0.95}
            }
        }}
    };

    FallClassificationBatch decoded;
    QVERIFY(fallClassificationBatchFromJson(json, &decoded));
    QCOMPARE(decoded.results.size(), 1);
    QCOMPARE(decoded.results.first().trackId, -1);
    QCOMPARE(decoded.results.first().iconId, -1);
    QCOMPARE(decoded.results.first().state, QStringLiteral("fall"));
    QCOMPARE(decoded.results.first().confidence, 0.95);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target fall_protocol_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host -R fall_protocol_test --output-on-failure
```

Expected: PASS with `fall_protocol_test` green.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/shared/models/fall_models.h \
        rk_app/src/shared/protocol/fall_ipc.cpp \
        rk_app/src/tests/protocol_tests/fall_protocol_test.cpp
git commit -m "Add overlay metadata to fall batch protocol"
```

### Task 2: Add stable icon allocation and publish overlay metadata from `health-falld`

**Files:**
- Create: `rk_app/src/health_falld/tracking/track_icon_registry.h`
- Create: `rk_app/src/health_falld/tracking/track_icon_registry.cpp`
- Modify: `rk_app/src/health_falld/CMakeLists.txt`
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.h`
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.cpp`
- Create: `rk_app/src/tests/fall_daemon_tests/track_icon_registry_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`

- [ ] **Step 1: Write the failing icon registry unit test**

Create `rk_app/src/tests/fall_daemon_tests/track_icon_registry_test.cpp` with:

```cpp
#include "tracking/track_icon_registry.h"

#include <QtTest/QTest>

class TrackIconRegistryTest : public QObject {
    Q_OBJECT

private slots:
    void keepsStableIconIdsForLiveTracks();
};

void TrackIconRegistryTest::keepsStableIconIdsForLiveTracks() {
    TrackIconRegistry registry(5);

    QCOMPARE(registry.iconIdForTrack(10), 1);
    QCOMPARE(registry.iconIdForTrack(20), 2);
    QCOMPARE(registry.iconIdForTrack(10), 1);

    registry.reconcileActiveTracks({20, 30});
    QCOMPARE(registry.iconIdForTrack(20), 2);
    QCOMPARE(registry.iconIdForTrack(30), 1);
}

QTEST_MAIN(TrackIconRegistryTest)
#include "track_icon_registry_test.moc"
```

Register it in `rk_app/src/tests/CMakeLists.txt` as a new executable linked against `QtCore` and `QtTest`.

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target track_icon_registry_test -j4
```

Expected: FAIL because `track_icon_registry.h/.cpp` and target wiring do not exist yet.

- [ ] **Step 3: Implement the registry**

Create `rk_app/src/health_falld/tracking/track_icon_registry.h`:

```cpp
#pragma once

#include <QHash>
#include <QSet>
#include <QVector>

class TrackIconRegistry {
public:
    explicit TrackIconRegistry(int maxIcons);

    int iconIdForTrack(int trackId);
    void reconcileActiveTracks(const QVector<int> &activeTrackIds);

private:
    int takeNextIconId();

    int maxIcons_ = 0;
    QHash<int, int> trackToIcon_;
    QSet<int> usedIcons_;
};
```

Create `rk_app/src/health_falld/tracking/track_icon_registry.cpp`:

```cpp
#include "tracking/track_icon_registry.h"

TrackIconRegistry::TrackIconRegistry(int maxIcons)
    : maxIcons_(maxIcons) {
}

int TrackIconRegistry::iconIdForTrack(int trackId) {
    if (trackToIcon_.contains(trackId)) {
        return trackToIcon_.value(trackId);
    }
    const int iconId = takeNextIconId();
    trackToIcon_.insert(trackId, iconId);
    usedIcons_.insert(iconId);
    return iconId;
}

void TrackIconRegistry::reconcileActiveTracks(const QVector<int> &activeTrackIds) {
    QSet<int> activeSet(activeTrackIds.cbegin(), activeTrackIds.cend());
    auto it = trackToIcon_.begin();
    while (it != trackToIcon_.end()) {
        if (!activeSet.contains(it.key())) {
            usedIcons_.remove(it.value());
            it = trackToIcon_.erase(it);
            continue;
        }
        ++it;
    }
}

int TrackIconRegistry::takeNextIconId() {
    for (int iconId = 1; iconId <= maxIcons_; ++iconId) {
        if (!usedIcons_.contains(iconId)) {
            return iconId;
        }
    }
    return maxIcons_ > 0 ? maxIcons_ : 1;
}
```

Wire the files into `rk_app/src/health_falld/CMakeLists.txt` and the new test target into `rk_app/src/tests/CMakeLists.txt`.

- [ ] **Step 4: Write the failing daemon integration assertion**

In `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`, extend the existing multi-person batch case so it asserts one result entry contains overlay metadata:

```cpp
QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification_batch"));
const QJsonArray results = json.value(QStringLiteral("results")).toArray();
QVERIFY(results.size() >= 2);
const QJsonObject first = results.first().toObject();
QVERIFY(first.contains(QStringLiteral("track_id")));
QVERIFY(first.contains(QStringLiteral("icon_id")));
QVERIFY(first.contains(QStringLiteral("anchor_x")));
QVERIFY(first.contains(QStringLiteral("anchor_y")));
QVERIFY(first.contains(QStringLiteral("bbox_x")));
QVERIFY(first.contains(QStringLiteral("bbox_h")));
```

- [ ] **Step 5: Run test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target track_icon_registry_test fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host -R 'track_icon_registry_test|fall_end_to_end_status_test' --output-on-failure
```

Expected: `track_icon_registry_test` passes after Step 3, but `fall_end_to_end_status_test` fails because the batch still lacks overlay metadata.

- [ ] **Step 6: Implement minimal daemon publishing**

Update `rk_app/src/health_falld/app/fall_daemon_app.h` to add:

```cpp
#include "tracking/track_icon_registry.h"
...
TrackIconRegistry trackIconRegistry_;
```

Initialize it in the constructor initializer list:

```cpp
, trackIconRegistry_(config_.maxTracks)
```

In `rk_app/src/health_falld/app/fall_daemon_app.cpp`, after `tracker_.update(...)`, reconcile active tracks:

```cpp
QVector<int> activeTrackIds;
for (const TrackedPerson &track : tracks) {
    activeTrackIds.push_back(track.trackId);
}
trackIconRegistry_.reconcileActiveTracks(activeTrackIds);
```

Add local helpers near the file top:

```cpp
QPointF overlayAnchorForPose(const PosePerson &person) {
    const QVector<int> preferred = {0, 1, 2, 3, 4};
    QVector<QPointF> points;
    for (int index : preferred) {
        if (index < person.keypoints.size() && person.keypoints[index].score > 0.2f) {
            points.push_back(QPointF(person.keypoints[index].x, person.keypoints[index].y));
        }
    }
    if (!points.isEmpty()) {
        QPointF sum;
        for (const QPointF &point : points) {
            sum += point;
        }
        return sum / points.size();
    }
    return QPointF(person.box.center().x(), person.box.top());
}
```

When building `FallClassificationEntry`, populate:

```cpp
const QPointF anchor = overlayAnchorForPose(track.latestPose);
entry.trackId = track.trackId;
entry.iconId = trackIconRegistry_.iconIdForTrack(track.trackId);
entry.anchorX = anchor.x();
entry.anchorY = anchor.y();
entry.bboxX = track.latestPose.box.x();
entry.bboxY = track.latestPose.box.y();
entry.bboxW = track.latestPose.box.width();
entry.bboxH = track.latestPose.box.height();
```

- [ ] **Step 7: Run tests to verify they pass**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target track_icon_registry_test fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host -R 'track_icon_registry_test|fall_end_to_end_status_test' --output-on-failure
```

Expected: both tests PASS.

- [ ] **Step 8: Commit**

```bash
git add rk_app/src/health_falld/CMakeLists.txt \
        rk_app/src/health_falld/app/fall_daemon_app.h \
        rk_app/src/health_falld/app/fall_daemon_app.cpp \
        rk_app/src/health_falld/tracking/track_icon_registry.h \
        rk_app/src/health_falld/tracking/track_icon_registry.cpp \
        rk_app/src/tests/CMakeLists.txt \
        rk_app/src/tests/fall_daemon_tests/track_icon_registry_test.cpp \
        rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
git commit -m "Publish stable track icon overlay metadata"
```

### Task 3: Teach the UI IPC layer and monitor page to carry icon overlay data

**Files:**
- Modify: `rk_app/src/health_ui/ipc_client/fall_ipc_client.h`
- Modify: `rk_app/src/health_ui/ipc_client/fall_ipc_client.cpp`
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.h`
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.cpp`
- Test: `rk_app/src/tests/ui_tests/fall_ipc_client_test.cpp`
- Test: `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`

- [ ] **Step 1: Write the failing UI IPC decode test**

Add this case to `rk_app/src/tests/ui_tests/fall_ipc_client_test.cpp`:

```cpp
void FallIpcClientTest::emitsBatchWithOverlayMetadata() {
    QLocalServer server;
    QVERIFY(server.listen(QStringLiteral("/tmp/rk_fall_ui_overlay.sock")));
    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_ui_overlay.sock"));

    FallIpcClient client;
    QSignalSpy batchSpy(&client, SIGNAL(classificationBatchReceived(FallClassificationBatch)));
    client.start();

    QTRY_VERIFY(server.hasPendingConnections() || server.waitForNewConnection(1000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);

    socket->write(
        "{\"type\":\"classification_batch\",\"camera_id\":\"front_cam\",\"ts\":1777000000000,\"results\":[{\"track_id\":3,\"icon_id\":1,\"state\":\"stand\",\"confidence\":0.98,\"anchor_x\":312,\"anchor_y\":118,\"bbox_x\":250,\"bbox_y\":90,\"bbox_w\":120,\"bbox_h\":260}]}\n");
    socket->flush();

    QTRY_COMPARE(batchSpy.count(), 1);
    const FallClassificationBatch batch = qvariant_cast<FallClassificationBatch>(batchSpy.takeFirst().at(0));
    QCOMPARE(batch.results.first().iconId, 1);
    QCOMPARE(batch.results.first().anchorX, 312.0);
    QCOMPARE(batch.results.first().bboxH, 260.0);

    qunsetenv("RK_FALL_SOCKET_NAME");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target fall_ipc_client_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host -R fall_ipc_client_test --output-on-failure
```

Expected: FAIL until shared protocol changes from Task 1 are visible in the UI test build or decode path still ignores the new fields.

- [ ] **Step 3: Write the failing page-state test**

Add this case to `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`:

```cpp
void VideoMonitorPageTest::rendersTrackIconsInStatusListAndForwardsOverlayEntries() {
    FakeVideoClient videoClient;
    FakeFallClient fallClient;
    VideoMonitorPage page(&videoClient, &fallClient);
    page.show();

    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1777000000000;

    FallClassificationEntry entry;
    entry.trackId = 3;
    entry.iconId = 1;
    entry.state = QStringLiteral("fall");
    entry.confidence = 0.94;
    entry.anchorX = 312.0;
    entry.anchorY = 118.0;
    entry.bboxX = 250.0;
    entry.bboxY = 90.0;
    entry.bboxW = 120.0;
    entry.bboxH = 260.0;
    batch.results.push_back(entry);

    emit fallClient.classificationBatchReceived(batch);

    QLabel *overlayLabel = page.findChild<QLabel*>(QStringLiteral("fallStatusLabel"));
    QVERIFY(overlayLabel != nullptr);
    QVERIFY(overlayLabel->text().contains(QStringLiteral("[1]")));
    QVERIFY(overlayLabel->text().contains(QStringLiteral("fall")));

    VideoPreviewWidget *preview = page.findChild<VideoPreviewWidget*>(QStringLiteral("videoPreviewWidget"));
    QVERIFY(preview != nullptr);
    QCOMPARE(preview->overlayEntries().size(), 1);
    QCOMPARE(preview->overlayEntries().first().iconId, 1);
}
```

- [ ] **Step 4: Run test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target video_monitor_page_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host -R video_monitor_page_test --output-on-failure
```

Expected: FAIL because the page and preview widget do not yet carry overlay entries.

- [ ] **Step 5: Implement minimal UI data propagation**

In `rk_app/src/health_ui/pages/video_monitor_page.h`, add an internal helper struct if the widget does not already define one:

```cpp
struct PreviewOverlayEntry {
    int iconId = -1;
    QString state;
    double confidence = 0.0;
    double anchorX = 0.0;
    double anchorY = 0.0;
    QRectF bbox;
};
```

Update `rk_app/src/health_ui/pages/video_monitor_page.cpp` in the batch handler to build label text like:

```cpp
lines.push_back(QStringLiteral("[%1] %2 %3")
    .arg(entry.iconId)
    .arg(entry.state)
    .arg(QString::number(entry.confidence, 'f', 2)));
```

And push entries into the preview widget:

```cpp
QVector<VideoPreviewWidget::OverlayEntry> overlayEntries;
for (const FallClassificationEntry &entry : batch.results) {
    VideoPreviewWidget::OverlayEntry overlay;
    overlay.iconId = entry.iconId;
    overlay.state = entry.state;
    overlay.confidence = entry.confidence;
    overlay.anchor = QPointF(entry.anchorX, entry.anchorY);
    overlay.bbox = QRectF(entry.bboxX, entry.bboxY, entry.bboxW, entry.bboxH);
    overlayEntries.push_back(overlay);
}
previewWidget_->setOverlayEntries(overlayEntries);
```

- [ ] **Step 6: Run tests to verify they pass**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target fall_ipc_client_test video_monitor_page_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host -R 'fall_ipc_client_test|video_monitor_page_test' --output-on-failure
```

Expected: PASS with both UI tests green.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_ui/ipc_client/fall_ipc_client.h \
        rk_app/src/health_ui/ipc_client/fall_ipc_client.cpp \
        rk_app/src/health_ui/pages/video_monitor_page.h \
        rk_app/src/health_ui/pages/video_monitor_page.cpp \
        rk_app/src/tests/ui_tests/fall_ipc_client_test.cpp \
        rk_app/src/tests/ui_tests/video_monitor_page_test.cpp
git commit -m "Propagate track icon overlay data into video monitor page"
```

### Task 4: Render the icon overlay in `VideoPreviewWidget`

**Files:**
- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.h`
- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.cpp`
- Modify: `rk_app/src/tests/ui_tests/video_preview_widget_test.cpp`

- [ ] **Step 1: Write the failing widget test**

Add this case to `rk_app/src/tests/ui_tests/video_preview_widget_test.cpp`:

```cpp
void VideoPreviewWidgetTest::storesAndClearsOverlayEntries() {
    VideoPreviewWidget widget;

    VideoPreviewWidget::OverlayEntry entry;
    entry.iconId = 1;
    entry.state = QStringLiteral("fall");
    entry.confidence = 0.94;
    entry.anchor = QPointF(120.0, 80.0);
    entry.bbox = QRectF(80.0, 60.0, 100.0, 220.0);

    widget.setOverlayEntries({entry});
    QCOMPARE(widget.overlayEntries().size(), 1);
    QCOMPARE(widget.overlayEntries().first().iconId, 1);

    widget.setOverlayEntries({});
    QVERIFY(widget.overlayEntries().isEmpty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target video_preview_widget_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host -R video_preview_widget_test --output-on-failure
```

Expected: FAIL because `OverlayEntry`, `setOverlayEntries()`, and `overlayEntries()` do not exist.

- [ ] **Step 3: Implement minimal widget state API**

In `rk_app/src/health_ui/widgets/video_preview_widget.h`, add:

```cpp
struct OverlayEntry {
    int iconId = -1;
    QString state;
    double confidence = 0.0;
    QPointF anchor;
    QRectF bbox;
};

void setOverlayEntries(const QVector<OverlayEntry> &entries);
QVector<OverlayEntry> overlayEntries() const;
```

Store the entries in a private member and call `update()` from the setter.

- [ ] **Step 4: Paint the overlay**

In `rk_app/src/health_ui/widgets/video_preview_widget.cpp`, inside `paintEvent`, after drawing the current frame, add:

```cpp
QPainter painter(this);
...
for (const OverlayEntry &entry : overlayEntries_) {
    const QPointF scaled = scalePointFromSource(entry.anchor);
    QRectF badgeRect(scaled.x() - 16.0, scaled.y() - 42.0, 32.0, 24.0);

    QColor fill = colorForIcon(entry.iconId);
    if (entry.state == QStringLiteral("fall")) {
        painter.setPen(QPen(QColor("#d62828"), 2.0));
    } else {
        painter.setPen(Qt::NoPen);
    }
    painter.setBrush(fill);
    painter.drawRoundedRect(badgeRect, 12.0, 12.0);

    painter.setPen(Qt::white);
    painter.drawText(badgeRect, Qt::AlignCenter, QString::number(entry.iconId));
}
```

Implement `scalePointFromSource()` using the same source-to-widget scaling logic already used by the preview image. Use `entry.bbox.center()` fallback only if anchor is invalid.

- [ ] **Step 5: Run test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target video_preview_widget_test video_monitor_page_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host -R 'video_preview_widget_test|video_monitor_page_test' --output-on-failure
```

Expected: PASS with both widget and page tests green.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/health_ui/widgets/video_preview_widget.h \
        rk_app/src/health_ui/widgets/video_preview_widget.cpp \
        rk_app/src/tests/ui_tests/video_preview_widget_test.cpp
git commit -m "Render track icon overlay in video preview"
```

### Task 5: Run the focused regression suite and board-oriented verification prep

**Files:**
- Modify: none unless small test fixes are required
- Verify: `/tmp/rk_health_station-track-icon-overlay-host`
- Verify: `deploy/scripts/build_rk3588_bundle.sh`

- [ ] **Step 1: Run the focused host regression suite**

Run:

```bash
cmake --build /tmp/rk_health_station-track-icon-overlay-host --target \
  fall_protocol_test \
  track_icon_registry_test \
  fall_end_to_end_status_test \
  fall_ipc_client_test \
  video_monitor_page_test \
  video_preview_widget_test -j4
ctest --test-dir /tmp/rk_health_station-track-icon-overlay-host \
  -R 'fall_protocol_test|track_icon_registry_test|fall_end_to_end_status_test|fall_ipc_client_test|video_monitor_page_test|video_preview_widget_test' \
  --output-on-failure
```

Expected: PASS with all six tests green.

- [ ] **Step 2: Build the RK3588 bundle**

Run:

```bash
bash deploy/scripts/build_rk3588_bundle.sh
```

Expected: `out/rk3588_bundle/` refreshed successfully and `health-falld`, `health-ui`, `health-videod`, `healthd` remain `ARM aarch64`.

- [ ] **Step 3: Record manual board verification checklist in the commit message body or handoff notes**

Use this checklist verbatim in the final handoff notes:

```text
Board verification:
1. Deploy `out/rk3588_bundle/` to the RK3588 bundle directory.
2. Start backend services.
3. Replay a multi-person test video.
4. Confirm each detected person shows a numbered icon above the head.
5. Confirm the status list shows the same numbered icon beside each state row.
6. Confirm the icon remains stable while the backend track remains stable.
7. Confirm `fall` rows are still highlighted red and the icon remains readable.
```

- [ ] **Step 4: Commit final verification-only changes if any were needed**

If no files changed, skip committing. If a small test-only fix was required, commit it with:

```bash
git add <files>
git commit -m "Fix track icon overlay verification regressions"
```

## Self-Review Checklist

- Spec coverage:
  - Overlay metadata fields: Task 1
  - Stable `iconId`: Task 2
  - Head-anchor output: Task 2
  - Status-list icon display: Task 3
  - Preview overlay rendering: Task 4
  - Focused regression + board prep: Task 5
- Placeholder scan:
  - No `TBD`, `TODO`, or “similar to Task N” shortcuts remain.
- Type consistency:
  - Shared protocol fields use `trackId`, `iconId`, `anchorX`, `anchorY`, `bboxX`, `bboxY`, `bboxW`, `bboxH` throughout.
  - UI widget uses `OverlayEntry` with `iconId`, `state`, `confidence`, `anchor`, and `bbox` consistently.

Plan complete and saved to `rk_app/docs/superpowers/plans/2026-04-20-track-icon-overlay-implementation.md`. Two execution options:

1. Subagent-Driven (recommended) - I dispatch a fresh subagent per task, review between tasks, fast iteration
2. Inline Execution - Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?

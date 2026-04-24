# Backend Runtime Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add sparse, useful backend runtime/performance logs for `health-videod` and `health-falld`, while trimming UI logging down to errors and connection changes.

**Architecture:** Introduce two tiny, testable aggregation helpers: one for video publish-side summaries and one for fall inference-side summaries. Keep immediate logs for important state transitions, suppress high-frequency steady-state logs, and emit compact 5-second summaries only when there has been activity.

**Tech Stack:** Qt 5 / C++17, Qt Test, existing `qInfo/qWarning/qCritical` logging, existing RK3588 board smoke workflow.

---

## File Map

- Create: `rk_app/src/health_videod/debug/video_runtime_log_stats.h` — summary window state and formatting for videod
- Create: `rk_app/src/health_videod/debug/video_runtime_log_stats.cpp` — videod aggregation implementation
- Create: `rk_app/src/health_falld/debug/fall_runtime_log_stats.h` — summary window state and formatting for falld
- Create: `rk_app/src/health_falld/debug/fall_runtime_log_stats.cpp` — falld aggregation implementation
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h` — add per-camera stats members and event hooks
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp` — emit lifecycle/error logs and periodic `video_perf`
- Modify: `rk_app/src/health_videod/core/video_service.cpp` — add sparse state-change logs for preview/record/test transitions
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.h` — add stats members and last-logged-state trackers
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.cpp` — emit model/connect/error/event logs and periodic `fall_perf`; suppress per-frame spam
- Modify: `rk_app/src/health_ui/app/ui_app.cpp` — keep connection-change logs, remove routine info spam
- Modify: `rk_app/src/health_ui/ipc_client/video_ipc_client.cpp` — keep connect/disconnect/errors, remove request/response chatter
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.cpp` — keep command failure/preview error handling, remove info-level steady-state logs
- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.cpp` — keep preview errors, remove normal source-change chatter if needed
- Modify: `rk_app/src/tests/CMakeLists.txt` — register the new logging helper tests
- Create: `rk_app/src/tests/video_daemon_tests/video_runtime_log_stats_test.cpp` — TDD for videod summary throttling
- Create: `rk_app/src/tests/fall_daemon_tests/fall_runtime_log_stats_test.cpp` — TDD for falld summary throttling/state suppression

### Task 1: Add video-side summary helper with TDD

**Files:**
- Create: `rk_app/src/health_videod/debug/video_runtime_log_stats.h`
- Create: `rk_app/src/health_videod/debug/video_runtime_log_stats.cpp`
- Create: `rk_app/src/tests/video_daemon_tests/video_runtime_log_stats_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
#include "debug/video_runtime_log_stats.h"

#include <QtTest/QTest>

class VideoRuntimeLogStatsTest : public QObject {
    Q_OBJECT

private slots:
    void doesNotEmitSummaryBeforeInterval();
    void emitsSummaryAfterIntervalWhenFramesFlow();
    void carriesDropDeltaAndLifetimeDropCount();
};

void VideoRuntimeLogStatsTest::doesNotEmitSummaryBeforeInterval() {
    VideoRuntimeLogStats stats(5000);
    stats.onDescriptorPublished(QStringLiteral("front_cam"), QStringLiteral("test_file"), true, 0, 1000);
    QVERIFY(!stats.takeSummaryIfDue(4999).has_value());
}

void VideoRuntimeLogStatsTest::emitsSummaryAfterIntervalWhenFramesFlow() {
    VideoRuntimeLogStats stats(5000);
    for (int i = 0; i < 10; ++i) {
        stats.onDescriptorPublished(QStringLiteral("front_cam"), QStringLiteral("test_file"), true, 0, 1000 + (i * 100));
    }

    const auto summary = stats.takeSummaryIfDue(6000);
    QVERIFY(summary.has_value());
    QCOMPARE(summary->cameraId, QStringLiteral("front_cam"));
    QCOMPARE(summary->inputMode, QStringLiteral("test_file"));
    QVERIFY(summary->publishFps > 0.0);
    QCOMPARE(summary->publishedFramesWindow, 10);
}

void VideoRuntimeLogStatsTest::carriesDropDeltaAndLifetimeDropCount() {
    VideoRuntimeLogStats stats(5000);
    stats.onDescriptorPublished(QStringLiteral("front_cam"), QStringLiteral("camera"), true, 3, 1000);
    stats.onDescriptorPublished(QStringLiteral("front_cam"), QStringLiteral("camera"), true, 5, 2000);

    const auto summary = stats.takeSummaryIfDue(7000);
    QVERIFY(summary.has_value());
    QCOMPARE(summary->droppedFramesTotal, quint64(5));
    QCOMPARE(summary->droppedFramesDelta, quint64(5));
}

QTEST_MAIN(VideoRuntimeLogStatsTest)
#include "video_runtime_log_stats_test.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir out/build-rk_app-host -R video_runtime_log_stats_test --output-on-failure`
Expected: FAIL because the new target/files do not exist yet.

- [ ] **Step 3: Add the target to CMake**

```cmake
add_executable(video_runtime_log_stats_test
    video_daemon_tests/video_runtime_log_stats_test.cpp
    ../health_videod/debug/video_runtime_log_stats.cpp
)
set_target_properties(video_runtime_log_stats_test PROPERTIES
    AUTOMOC ON
)
target_include_directories(video_runtime_log_stats_test PRIVATE
    ..
    ../health_videod
)
target_link_libraries(video_runtime_log_stats_test PRIVATE
    Qt5::Core
    Qt5::Test
)
add_test(NAME video_runtime_log_stats_test COMMAND video_runtime_log_stats_test)
```

- [ ] **Step 4: Write the minimal helper interface**

```cpp
#pragma once

#include <optional>
#include <QString>

struct VideoRuntimeLogSummary {
    QString cameraId;
    QString inputMode;
    bool consumerConnected = false;
    int publishedFramesWindow = 0;
    double publishFps = 0.0;
    quint64 droppedFramesTotal = 0;
    quint64 droppedFramesDelta = 0;
};

class VideoRuntimeLogStats {
public:
    explicit VideoRuntimeLogStats(qint64 intervalMs = 5000);

    void onDescriptorPublished(const QString &cameraId, const QString &inputMode,
        bool consumerConnected, quint64 droppedFramesTotal, qint64 nowMs);
    std::optional<VideoRuntimeLogSummary> takeSummaryIfDue(qint64 nowMs);

private:
    qint64 intervalMs_ = 5000;
    qint64 windowStartMs_ = -1;
    QString cameraId_;
    QString inputMode_;
    bool consumerConnected_ = false;
    int publishedFramesWindow_ = 0;
    quint64 droppedFramesTotal_ = 0;
    quint64 droppedFramesWindowStart_ = 0;
};
```

- [ ] **Step 5: Write the minimal implementation**

```cpp
#include "debug/video_runtime_log_stats.h"

VideoRuntimeLogStats::VideoRuntimeLogStats(qint64 intervalMs)
    : intervalMs_(intervalMs) {
}

void VideoRuntimeLogStats::onDescriptorPublished(const QString &cameraId, const QString &inputMode,
    bool consumerConnected, quint64 droppedFramesTotal, qint64 nowMs) {
    if (windowStartMs_ < 0) {
        windowStartMs_ = nowMs;
        droppedFramesWindowStart_ = droppedFramesTotal;
    }
    cameraId_ = cameraId;
    inputMode_ = inputMode;
    consumerConnected_ = consumerConnected;
    droppedFramesTotal_ = droppedFramesTotal;
    publishedFramesWindow_ += 1;
}

std::optional<VideoRuntimeLogSummary> VideoRuntimeLogStats::takeSummaryIfDue(qint64 nowMs) {
    if (windowStartMs_ < 0 || publishedFramesWindow_ == 0 || (nowMs - windowStartMs_) < intervalMs_) {
        return std::nullopt;
    }

    const qint64 elapsedMs = qMax<qint64>(1, nowMs - windowStartMs_);
    VideoRuntimeLogSummary summary;
    summary.cameraId = cameraId_;
    summary.inputMode = inputMode_;
    summary.consumerConnected = consumerConnected_;
    summary.publishedFramesWindow = publishedFramesWindow_;
    summary.publishFps = (publishedFramesWindow_ * 1000.0) / elapsedMs;
    summary.droppedFramesTotal = droppedFramesTotal_;
    summary.droppedFramesDelta = droppedFramesTotal_ - droppedFramesWindowStart_;

    windowStartMs_ = nowMs;
    publishedFramesWindow_ = 0;
    droppedFramesWindowStart_ = droppedFramesTotal_;
    return summary;
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `ctest --test-dir out/build-rk_app-host -R video_runtime_log_stats_test --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_videod/debug/video_runtime_log_stats.h rk_app/src/health_videod/debug/video_runtime_log_stats.cpp rk_app/src/tests/video_daemon_tests/video_runtime_log_stats_test.cpp rk_app/src/tests/CMakeLists.txt
git commit -m "test: add videod runtime logging stats helper"
```

### Task 2: Integrate sparse videod lifecycle logs and periodic `video_perf`

**Files:**
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/health_videod/core/video_service.cpp`

- [ ] **Step 1: Add stats state to the active pipeline**

```cpp
#include "debug/video_runtime_log_stats.h"

struct ActivePipeline {
    QProcess *process = nullptr;
    bool recording = false;
    bool testInput = false;
    QString previewUrl;
    QString cameraId;
    int analysisWidth = 0;
    int analysisHeight = 0;
    int analysisFrameBytes = 0;
    quint64 nextFrameId = 1;
    QByteArray stdoutBuffer;
    SharedMemoryFrameRingWriter *frameRing = nullptr;
    VideoRuntimeLogStats logStats;
};
```

- [ ] **Step 2: Add immediate lifecycle logs in `VideoService`**

```cpp
qInfo().noquote()
    << QStringLiteral("video_runtime camera=%1 event=test_input_started file=%2")
           .arg(cameraId)
           .arg(channel.testFilePath);

qInfo().noquote()
    << QStringLiteral("video_runtime camera=%1 event=test_input_stopped")
           .arg(cameraId);

qInfo().noquote()
    << QStringLiteral("video_runtime camera=%1 event=recording_started path=%2")
           .arg(cameraId)
           .arg(outputPath);
```

Add these only on success paths and one warning log on failure paths:

```cpp
qWarning().noquote()
    << QStringLiteral("video_runtime camera=%1 event=start_test_input_failed error=%2")
           .arg(cameraId)
           .arg(QStringLiteral("test_input_start_failed"));
```

- [ ] **Step 3: Emit periodic `video_perf` from descriptor publication path**

```cpp
const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
pipeline.logStats.onDescriptorPublished(
    packet.cameraId,
    pipeline.testInput ? QStringLiteral("test_file") : QStringLiteral("camera"),
    analysisFrameSource_ && analysisFrameSource_->acceptsFrames(packet.cameraId),
    pipeline.frameRing ? pipeline.frameRing->droppedFrames() : 0,
    nowMs);

if (const auto summary = pipeline.logStats.takeSummaryIfDue(nowMs)) {
    qInfo().noquote()
        << QStringLiteral(
               "video_perf camera=%1 mode=%2 fps=%3 published=%4 dropped_total=%5 dropped_delta=%6 consumers=%7")
               .arg(summary->cameraId)
               .arg(summary->inputMode)
               .arg(QString::number(summary->publishFps, 'f', 1))
               .arg(summary->publishedFramesWindow)
               .arg(summary->droppedFramesTotal)
               .arg(summary->droppedFramesDelta)
               .arg(summary->consumerConnected ? 1 : 0);
}
```

- [ ] **Step 4: Log pipeline terminal events only once per transition**

```cpp
qInfo().noquote()
    << QStringLiteral("video_runtime camera=%1 event=preview_started mode=%2 analysis=%3")
           .arg(cameraId)
           .arg(pipeline.testInput ? QStringLiteral("test_file") : QStringLiteral("camera"))
           .arg(pipeline.analysisFrameBytes > 0 ? 1 : 0);

qInfo().noquote()
    << QStringLiteral("video_runtime camera=%1 event=playback_finished")
           .arg(cameraId);

qWarning().noquote()
    << QStringLiteral("video_runtime camera=%1 event=pipeline_error error=%2")
           .arg(cameraId)
           .arg(QStringLiteral("preview_pipeline_failed"));
```

- [ ] **Step 5: Build the videod targets and verify no compilation regressions**

Run: `cmake --build out/build-rk_app-host --target video_service_test video_service_analysis_test -j4`
Expected: build succeeds.

- [ ] **Step 6: Run focused video tests**

Run: `ctest --test-dir out/build-rk_app-host -R 'video_service_test|video_service_analysis_test|video_runtime_log_stats_test' --output-on-failure`
Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp rk_app/src/health_videod/core/video_service.cpp
git commit -m "feat: add sparse videod runtime logs"
```

### Task 3: Add fall-side summary helper with TDD

**Files:**
- Create: `rk_app/src/health_falld/debug/fall_runtime_log_stats.h`
- Create: `rk_app/src/health_falld/debug/fall_runtime_log_stats.cpp`
- Create: `rk_app/src/tests/fall_daemon_tests/fall_runtime_log_stats_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
#include "debug/fall_runtime_log_stats.h"

#include <QtTest/QTest>

class FallRuntimeLogStatsTest : public QObject {
    Q_OBJECT

private slots:
    void doesNotEmitSummaryBeforeInterval();
    void emitsSummaryAfterInterval();
    void tracksPeopleEmptyAndBatchCounts();
};

void FallRuntimeLogStatsTest::doesNotEmitSummaryBeforeInterval() {
    FallRuntimeLogStats stats(5000);
    stats.onFrameIngested(1000);
    stats.onInferenceComplete(1000, true, false, 30.0);
    QVERIFY(!stats.takeSummaryIfDue(QStringLiteral("front_cam"), QStringLiteral("stand"), 0.9, QString(), 4999).has_value());
}

void FallRuntimeLogStatsTest::emitsSummaryAfterInterval() {
    FallRuntimeLogStats stats(5000);
    for (int i = 0; i < 6; ++i) {
        stats.onFrameIngested(1000 + (i * 200));
        stats.onInferenceComplete(1000 + (i * 200), true, true, 28.0);
    }

    const auto summary = stats.takeSummaryIfDue(
        QStringLiteral("front_cam"), QStringLiteral("stand"), 0.95, QString(), 7000);
    QVERIFY(summary.has_value());
    QVERIFY(summary->ingestFps > 0.0);
    QVERIFY(summary->inferFps > 0.0);
    QCOMPARE(summary->peopleFrames, 6);
    QCOMPARE(summary->nonEmptyBatchCount, 6);
}

void FallRuntimeLogStatsTest::tracksPeopleEmptyAndBatchCounts() {
    FallRuntimeLogStats stats(5000);
    stats.onFrameIngested(1000);
    stats.onInferenceComplete(1000, false, false, 25.0);
    stats.onFrameIngested(2000);
    stats.onInferenceComplete(2000, true, true, 35.0);

    const auto summary = stats.takeSummaryIfDue(
        QStringLiteral("front_cam"), QStringLiteral("monitoring"), 0.0, QString(), 7000);
    QVERIFY(summary.has_value());
    QCOMPARE(summary->peopleFrames, 1);
    QCOMPARE(summary->emptyFrames, 1);
    QCOMPARE(summary->nonEmptyBatchCount, 1);
    QCOMPARE(summary->avgInferMs, 30.0);
}

QTEST_MAIN(FallRuntimeLogStatsTest)
#include "fall_runtime_log_stats_test.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir out/build-rk_app-host -R fall_runtime_log_stats_test --output-on-failure`
Expected: FAIL because the new target/files do not exist yet.

- [ ] **Step 3: Add the target to CMake**

```cmake
add_executable(fall_runtime_log_stats_test
    fall_daemon_tests/fall_runtime_log_stats_test.cpp
    ../health_falld/debug/fall_runtime_log_stats.cpp
)
set_target_properties(fall_runtime_log_stats_test PROPERTIES
    AUTOMOC ON
)
target_include_directories(fall_runtime_log_stats_test PRIVATE
    ..
    ../health_falld
)
target_link_libraries(fall_runtime_log_stats_test PRIVATE
    Qt5::Core
    Qt5::Test
)
add_test(NAME fall_runtime_log_stats_test COMMAND fall_runtime_log_stats_test)
```

- [ ] **Step 4: Write the minimal helper interface/implementation**

```cpp
struct FallRuntimeLogSummary {
    QString cameraId;
    double ingestFps = 0.0;
    double inferFps = 0.0;
    double avgInferMs = 0.0;
    int peopleFrames = 0;
    int emptyFrames = 0;
    int nonEmptyBatchCount = 0;
    QString latestState;
    double latestConfidence = 0.0;
    QString latestError;
};

class FallRuntimeLogStats {
public:
    explicit FallRuntimeLogStats(qint64 intervalMs = 5000);
    void onFrameIngested(qint64 nowMs);
    void onInferenceComplete(qint64 nowMs, bool hasPeople, bool nonEmptyBatch, double inferMs);
    std::optional<FallRuntimeLogSummary> takeSummaryIfDue(
        const QString &cameraId, const QString &latestState, double latestConfidence,
        const QString &latestError, qint64 nowMs);
private:
    qint64 intervalMs_ = 5000;
    qint64 windowStartMs_ = -1;
    int ingestedFramesWindow_ = 0;
    int inferredFramesWindow_ = 0;
    int peopleFramesWindow_ = 0;
    int emptyFramesWindow_ = 0;
    int nonEmptyBatchWindow_ = 0;
    double inferMsTotalWindow_ = 0.0;
};
```

- [ ] **Step 5: Run test to verify it passes**

Run: `ctest --test-dir out/build-rk_app-host -R fall_runtime_log_stats_test --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/health_falld/debug/fall_runtime_log_stats.h rk_app/src/health_falld/debug/fall_runtime_log_stats.cpp rk_app/src/tests/fall_daemon_tests/fall_runtime_log_stats_test.cpp rk_app/src/tests/CMakeLists.txt
git commit -m "test: add falld runtime logging stats helper"
```

### Task 4: Integrate sparse falld logs and suppress steady-state spam

**Files:**
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.h`
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.cpp`

- [ ] **Step 1: Add stats and suppression state to `FallDaemonApp`**

```cpp
#include "debug/fall_runtime_log_stats.h"

FallRuntimeLogStats logStats_;
QString lastLoggedError_;
QString lastLoggedState_;
```

- [ ] **Step 2: Log model load and ingest connection changes**

```cpp
qInfo().noquote()
    << QStringLiteral("fall_runtime camera=%1 event=input_connected connected=%2")
           .arg(config_.cameraId)
           .arg(connected ? 1 : 0);

qInfo().noquote()
    << QStringLiteral("fall_runtime camera=%1 event=pose_model_loaded path=%2 ready=%3")
           .arg(config_.cameraId)
           .arg(config_.poseModelPath)
           .arg(runtimeStatus_.poseModelReady ? 1 : 0);
```

Log load failures with `qWarning()` and keep the existing status update behavior.

- [ ] **Step 3: Update counters during frame processing**

```cpp
const qint64 frameStartMs = QDateTime::currentMSecsSinceEpoch();
logStats_.onFrameIngested(frameStartMs);

const QVector<PosePerson> people = poseEstimator_->infer(frame, &error);
const qint64 inferDoneMs = QDateTime::currentMSecsSinceEpoch();
const double inferMs = inferDoneMs - frameStartMs;
```

Then after computing `tracks` / `batch`:

```cpp
const bool hasPeople = !tracks.isEmpty();
const bool nonEmptyBatch = !batch.results.isEmpty();
logStats_.onInferenceComplete(inferDoneMs, hasPeople, nonEmptyBatch, inferMs);
```

- [ ] **Step 4: Suppress noisy steady-state logs**

Replace unconditional `classification` / `classification_batch` info logs with conditional logging:

```cpp
const bool alertState = highestState == QStringLiteral("fall") || highestState == QStringLiteral("lie");
const bool stateChanged = highestState != lastLoggedState_;
if (alertState || stateChanged) {
    qInfo().noquote()
        << QStringLiteral("classification camera=%1 state=%2 confidence=%3 ts=%4")
               .arg(classification.cameraId)
               .arg(classification.state)
               .arg(QString::number(classification.confidence, 'f', 3))
               .arg(classification.timestampMs);
    lastLoggedState_ = highestState;
}
```

Keep `fall_event` immediate and unchanged in spirit.

- [ ] **Step 5: Emit periodic `fall_perf` summaries and deduplicate error logs**

```cpp
if (runtimeStatus_.lastError != lastLoggedError_) {
    if (runtimeStatus_.lastError.isEmpty()) {
        qInfo().noquote()
            << QStringLiteral("fall_runtime camera=%1 event=error_cleared")
                   .arg(config_.cameraId);
    } else {
        qWarning().noquote()
            << QStringLiteral("fall_runtime camera=%1 event=error error=%2")
                   .arg(config_.cameraId)
                   .arg(runtimeStatus_.lastError);
    }
    lastLoggedError_ = runtimeStatus_.lastError;
}

if (const auto summary = logStats_.takeSummaryIfDue(
        config_.cameraId, runtimeStatus_.latestState, runtimeStatus_.latestConfidence,
        runtimeStatus_.lastError, runtimeStatus_.lastInferTs)) {
    qInfo().noquote()
        << QStringLiteral(
               "fall_perf camera=%1 ingest_fps=%2 infer_fps=%3 avg_infer_ms=%4 people_frames=%5 empty_frames=%6 nonempty_batches=%7 state=%8 conf=%9 error=%10")
               .arg(summary->cameraId)
               .arg(QString::number(summary->ingestFps, 'f', 1))
               .arg(QString::number(summary->inferFps, 'f', 1))
               .arg(QString::number(summary->avgInferMs, 'f', 1))
               .arg(summary->peopleFrames)
               .arg(summary->emptyFrames)
               .arg(summary->nonEmptyBatchCount)
               .arg(summary->latestState)
               .arg(QString::number(summary->latestConfidence, 'f', 2))
               .arg(summary->latestError);
}
```

- [ ] **Step 6: Build and run focused fall tests**

Run: `cmake --build out/build-rk_app-host --target fall_runtime_log_stats_test fall_gateway_test -j4`
Expected: build succeeds.

Run: `ctest --test-dir out/build-rk_app-host -R 'fall_runtime_log_stats_test|fall_gateway_test' --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_falld/app/fall_daemon_app.h rk_app/src/health_falld/app/fall_daemon_app.cpp
git commit -m "feat: add sparse falld runtime logs"
```

### Task 5: Trim UI logs to errors and connection changes

**Files:**
- Modify: `rk_app/src/health_ui/app/ui_app.cpp`
- Modify: `rk_app/src/health_ui/ipc_client/video_ipc_client.cpp`
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.cpp`
- Modify: `rk_app/src/health_ui/widgets/video_preview_widget.cpp`

- [ ] **Step 1: Remove routine info logs from `UiApp`**

Keep only connection-result logs and remove periodic view-refresh chatter. The target shape is:

```cpp
qInfo() << "health-ui lifecycle: backend connection result" << connected;
qInfo() << "health-ui lifecycle: video backend connection result" << connected;
qInfo() << "health-ui lifecycle: fall backend connection result" << fallConnected;
```

Delete logs such as:

```cpp
qInfo() << "health-ui ui: dashboard snapshot received" << ...;
qInfo() << "health-ui ui: device list received" << ...;
```

- [ ] **Step 2: Remove request/response chatter from `VideoIpcClient`**

Keep:

```cpp
qInfo() << "health-ui video ipc: connecting to backend" << ...;
qInfo() << "health-ui video ipc: connect result" << ...;
qWarning() << "health-ui video ipc: invalid response frame";
qWarning() << "health-ui video ipc: failed to decode response";
qWarning() << "health-ui video ipc: skipped command because backend is disconnected" << ...;
```

Remove per-command request/response info logs.

- [ ] **Step 3: Remove steady-state info logs from `VideoMonitorPage` and preview widget**

Keep warning/error paths only:

```cpp
qWarning() << "health-ui video: preview error text" << text;
```

Drop info logs for status-received, command-finished-success, set-preview-source, and routine classification updates.

- [ ] **Step 4: Build the UI target**

Run: `cmake --build out/build-rk_app-host --target health-ui -j4`
Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add rk_app/src/health_ui/app/ui_app.cpp rk_app/src/health_ui/ipc_client/video_ipc_client.cpp rk_app/src/health_ui/pages/video_monitor_page.cpp rk_app/src/health_ui/widgets/video_preview_widget.cpp
git commit -m "refactor: trim ui runtime log noise"
```

### Task 6: End-to-end verification on host and RK3588 board

**Files:**
- No source changes required unless verification exposes a bug

- [ ] **Step 1: Run the focused host test suite**

Run:

```bash
ctest --test-dir out/build-rk_app-host -R 'video_runtime_log_stats_test|fall_runtime_log_stats_test|video_service_test|video_service_analysis_test|fall_gateway_test' --output-on-failure
```

Expected: all PASS.

- [ ] **Step 2: Build RK3588 binaries**

Run:

```bash
cmake --build /tmp/rk_health_station-build-rk3588-ui-debug --target health-ui health-videod health-falld -j4
```

Expected: build succeeds.

- [ ] **Step 3: Deploy the three binaries to the candidate bundle**

Run:

```bash
scp /tmp/rk_health_station-build-rk3588-ui-debug/src/health_ui/health-ui elf@192.168.137.179:/home/elf/rk3588_bundle_candidate/bin/
scp /tmp/rk_health_station-build-rk3588-ui-debug/src/health_videod/health-videod elf@192.168.137.179:/home/elf/rk3588_bundle_candidate/bin/
scp /tmp/rk_health_station-build-rk3588-ui-debug/src/health_falld/health-falld elf@192.168.137.179:/home/elf/rk3588_bundle_candidate/bin/
```

Expected: files copy successfully.

- [ ] **Step 4: Verify backend-only logs on board**

Run a backend-only test-mode session against `/home/elf/Videos/video.mp4` and confirm:
- `health-videod.log` includes sparse `video_runtime` events
- `health-videod.log` includes `video_perf` roughly every 5 seconds, not per frame
- `health-falld.log` includes sparse `fall_runtime` events
- `health-falld.log` includes `fall_perf` roughly every 5 seconds
- `health-falld.log` no longer prints one `stand` line per frame
- `fall_event` still appears immediately when present

- [ ] **Step 5: Verify UI logs stay quiet**

Run offscreen UI open-video-page smoke and confirm UI log contains only:
- connection result logs
- backend disconnect/connect logs
- preview errors if they occur

Expected: no per-frame classification chatter.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: add sparse backend performance logs"
```

## Self-Review

- Spec coverage: this plan covers backend periodic summaries, backend immediate lifecycle/error logs, suppression of falld steady-state spam, and UI log trimming.
- Placeholder scan: no `TODO`/`TBD` markers remain; each task has file paths, commands, and code snippets.
- Type consistency: both helper APIs are intentionally simple and injection-free so they can be unit-tested and then embedded into `GstreamerVideoPipelineBackend` and `FallDaemonApp` without changing external interfaces.

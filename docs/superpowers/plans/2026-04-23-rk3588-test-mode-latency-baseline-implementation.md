# RK3588 Test-Mode Latency Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a repeatable board-side latency measurement path for RK3588 test-mode playback, compare `main` versus the current worktree with `~/Videos/video.mp4`, and produce enough evidence to make the worktree startup classification latency no worse than `main`.

**Architecture:** Add lightweight timestamp marker instrumentation to `health-videod` and `health-falld`, expose those markers through append-only JSONL files, and drive both bundles with a host-side A/B harness. The harness computes playback-start, first-analysis-ingress, and first-classification latencies using the board clock so the result is comparable across builds.

**Tech Stack:** Qt Core/Network test targets, existing local socket IPC, JSONL marker files, Python 3 board-control harness over SSH/rsync, RK3588 bundle scripts.

---

## File Structure Map

### Shared instrumentation utility
- Create: `rk_app/src/shared/debug/latency_marker_writer.h` - reusable append-only JSONL marker writer used by multiple daemons.
- Create: `rk_app/src/shared/debug/latency_marker_writer.cpp` - environment/path-driven event writing with board timestamps and optional extra payload fields.
- Modify: `rk_app/src/shared/CMakeLists.txt` - compile the shared marker utility into `rk_shared`.
- Create: `rk_app/src/tests/protocol_tests/latency_marker_writer_test.cpp` - unit coverage for append behavior, disabled behavior, and payload preservation.

### Board-visible latency markers
- Modify: `rk_app/src/health_videod/core/video_service.cpp` - emit a test-mode playback-start marker exactly when `start_test_input` transitions the channel into `playing`.
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.cpp` - emit markers for first analysis-frame ingress and first classification event for each process run.
- Modify: `rk_app/src/tests/video_daemon_tests/video_service_test.cpp` - verify test-mode startup writes the expected marker.
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp` - verify first-frame and first-classification markers appear for a live fall-daemon run.

### Measurement harness and result artifact
- Create: `deploy/tests/measure_rk3588_test_mode_latency.py` - host-side script that deploys/starts a bundle, enables test mode with `~/Videos/video.mp4`, waits for marker files/classification, and prints structured latency JSON.
- Create: `deploy/tests/measure_rk3588_test_mode_latency_test.py` - unit tests for marker parsing and latency computation logic.
- Create: `docs/testing/2026-04-23-rk3588-test-mode-latency-baseline.md` - checked-in result note recording the measured `main` and worktree latencies plus the identified regression boundary.

---

### Task 1: Add a reusable JSONL latency marker writer to `rk_shared`

**Files:**
- Create: `rk_app/src/shared/debug/latency_marker_writer.h`
- Create: `rk_app/src/shared/debug/latency_marker_writer.cpp`
- Modify: `rk_app/src/shared/CMakeLists.txt`
- Create: `rk_app/src/tests/protocol_tests/latency_marker_writer_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing marker-writer test**

```cpp
#include "debug/latency_marker_writer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QTest>

class LatencyMarkerWriterTest : public QObject {
    Q_OBJECT

private slots:
    void appendsStructuredEventWhenEnabled();
    void ignoresWritesWhenPathIsEmpty();
};

void LatencyMarkerWriterTest::appendsStructuredEventWhenEnabled() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString markerPath = tempDir.filePath(QStringLiteral("latency.jsonl"));
    LatencyMarkerWriter writer(markerPath);

    writer.writeEvent(QStringLiteral("playback_started"), 1777000000123,
        QJsonObject{{QStringLiteral("camera_id"), QStringLiteral("front_cam")}});

    QFile file(markerPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray line = file.readLine().trimmed();
    const QJsonObject json = QJsonDocument::fromJson(line).object();
    QCOMPARE(json.value(QStringLiteral("event")).toString(), QStringLiteral("playback_started"));
    QCOMPARE(static_cast<qint64>(json.value(QStringLiteral("ts_ms")).toDouble()), 1777000000123);
    QCOMPARE(json.value(QStringLiteral("camera_id")).toString(), QStringLiteral("front_cam"));
}

void LatencyMarkerWriterTest::ignoresWritesWhenPathIsEmpty() {
    LatencyMarkerWriter writer(QString());
    writer.writeEvent(QStringLiteral("ignored"), 1777000000999, {});
    QVERIFY(true);
}

QTEST_MAIN(LatencyMarkerWriterTest)
#include "latency_marker_writer_test.moc"
```

- [ ] **Step 2: Register the failing test target**

```cmake
add_executable(latency_marker_writer_test
    protocol_tests/latency_marker_writer_test.cpp
)

set_target_properties(latency_marker_writer_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(latency_marker_writer_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME latency_marker_writer_test COMMAND latency_marker_writer_test)
```

- [ ] **Step 3: Run the test to verify it fails before implementation**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target latency_marker_writer_test && \
ctest --test-dir out/build-rk_app-host-tests -R latency_marker_writer_test --output-on-failure
```

Expected: build fails because `debug/latency_marker_writer.h` and the corresponding implementation do not exist yet.

- [ ] **Step 4: Add the minimal shared marker writer**

```cpp
// rk_app/src/shared/debug/latency_marker_writer.h
#pragma once

#include <QJsonObject>
#include <QString>

class LatencyMarkerWriter {
public:
    explicit LatencyMarkerWriter(const QString &path);

    bool isEnabled() const;
    void writeEvent(const QString &event, qint64 timestampMs, const QJsonObject &payload) const;

private:
    QString path_;
};
```

```cpp
// rk_app/src/shared/debug/latency_marker_writer.cpp
#include "debug/latency_marker_writer.h"

#include <QFile>
#include <QJsonDocument>

LatencyMarkerWriter::LatencyMarkerWriter(const QString &path)
    : path_(path) {
}

bool LatencyMarkerWriter::isEnabled() const {
    return !path_.isEmpty();
}

void LatencyMarkerWriter::writeEvent(
    const QString &event, qint64 timestampMs, const QJsonObject &payload) const {
    if (!isEnabled()) {
        return;
    }

    QFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QJsonObject json = payload;
    json.insert(QStringLiteral("event"), event);
    json.insert(QStringLiteral("ts_ms"), timestampMs);
    const QByteArray line = QJsonDocument(json).toJson(QJsonDocument::Compact);
    file.write(line);
    file.write("\n");
}
```

```cmake
# rk_app/src/shared/CMakeLists.txt
add_library(rk_shared STATIC
    debug/latency_marker_writer.cpp
    protocol/device_frame.cpp
    protocol/ipc_message.cpp
    protocol/analysis_stream_protocol.cpp
    protocol/fall_ipc.cpp
    protocol/video_ipc.cpp
    security/hmac_helper.cpp
    storage/database.cpp
)
```

- [ ] **Step 5: Run the test to verify the writer passes**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target latency_marker_writer_test && \
ctest --test-dir out/build-rk_app-host-tests -R latency_marker_writer_test --output-on-failure
```

Expected: `latency_marker_writer_test` passes.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/shared/debug/latency_marker_writer.h \
        rk_app/src/shared/debug/latency_marker_writer.cpp \
        rk_app/src/shared/CMakeLists.txt \
        rk_app/src/tests/protocol_tests/latency_marker_writer_test.cpp \
        rk_app/src/tests/CMakeLists.txt
git commit -m "test: add reusable latency marker writer"
```

### Task 2: Emit playback-start, first-frame, and first-classification markers from the video and fall daemons

**Files:**
- Modify: `rk_app/src/health_videod/core/video_service.cpp`
- Modify: `rk_app/src/health_falld/app/fall_daemon_app.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/video_service_test.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`

- [ ] **Step 1: Write the failing video-service test for the playback-start marker**

```cpp
void VideoServiceTest::writesPlaybackStartMarkerWhenTestInputStarts() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("RK_VIDEO_LATENCY_MARKER_PATH",
        tempDir.filePath(QStringLiteral("video-latency.jsonl")).toUtf8());

    const QString path = QDir::temp().filePath(QStringLiteral("latency-demo.mp4"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    QVERIFY(service.startTestInput(QStringLiteral("front_cam"), path).ok);

    QFile marker(tempDir.filePath(QStringLiteral("video-latency.jsonl")));
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray content = marker.readAll();
    QVERIFY(content.contains("playback_started"));

    qunsetenv("RK_VIDEO_LATENCY_MARKER_PATH");
}
```

- [ ] **Step 2: Write the failing end-to-end fall test for first-frame and first-classification markers**

```cpp
void FallEndToEndStatusTest::writesLatencyMarkersForFirstFrameAndFirstClassification() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_latency.sock"));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("RK_FALL_LATENCY_MARKER_PATH",
        tempDir.filePath(QStringLiteral("fall-latency.jsonl")).toUtf8());

    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_latency.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_latency.sock")));

    FallDaemonApp app(std::make_unique<SinglePoseEstimator>());
    QVERIFY(app.start());

    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    streamJpegAnalysisFrames(analysisSocket, 1, 45);

    QFile marker(tempDir.filePath(QStringLiteral("fall-latency.jsonl")));
    QTRY_VERIFY_WITH_TIMEOUT(marker.exists(), 2000);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray content = marker.readAll();
    QVERIFY(content.contains("first_analysis_frame"));
    QVERIFY(content.contains("first_classification"));

    qunsetenv("RK_FALL_LATENCY_MARKER_PATH");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}
```

- [ ] **Step 3: Run the two tests to verify they fail before instrumentation exists**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target video_service_test fall_end_to_end_status_test && \
ctest --test-dir out/build-rk_app-host-tests -R "video_service_test|fall_end_to_end_status_test" --output-on-failure
```

Expected: the new assertions fail because no latency marker files are written yet.

- [ ] **Step 4: Add playback-start marker emission to `VideoService`**

```cpp
// rk_app/src/health_videod/core/video_service.cpp
namespace {
const char kVideoLatencyMarkerEnvVar[] = "RK_VIDEO_LATENCY_MARKER_PATH";
}

VideoCommandResult VideoService::startTestInput(const QString &cameraId, const QString &filePath) {
    // existing validation and state updates remain unchanged

    QString errorCode;
    if (!restartPreviewForChannel(cameraId, &errorCode)) {
        channels_[cameraId] = previous;
        Q_UNUSED(errorCode);
        return buildErrorResult(cameraId, QStringLiteral("start_test_input"),
            QStringLiteral("test_input_start_failed"));
    }

    LatencyMarkerWriter marker(qEnvironmentVariable(kVideoLatencyMarkerEnvVar));
    marker.writeEvent(QStringLiteral("playback_started"), QDateTime::currentMSecsSinceEpoch(),
        QJsonObject {
            {QStringLiteral("camera_id"), cameraId},
            {QStringLiteral("file_path"), channel.testFilePath},
            {QStringLiteral("input_mode"), channel.inputMode},
        });

    return buildOkResult(cameraId, QStringLiteral("start_test_input"),
        videoChannelStatusToJson(channels_.value(cameraId)));
}
```

- [ ] **Step 5: Add one-shot first-frame and first-classification markers to `FallDaemonApp`**

```cpp
// rk_app/src/health_falld/app/fall_daemon_app.cpp
namespace {
const char kFallLatencyMarkerEnvVar[] = "RK_FALL_LATENCY_MARKER_PATH";
}

FallDaemonApp::FallDaemonApp(std::unique_ptr<PoseEstimator> poseEstimator, QObject *parent)
    : QObject(parent)
    , config_(loadFallRuntimeConfig())
    , poseEstimator_(std::move(poseEstimator))
    // existing fields...
{
    const QString markerPath = qEnvironmentVariable(kFallLatencyMarkerEnvVar);
    latencyMarkerWriter_ = std::make_unique<LatencyMarkerWriter>(markerPath);

    connect(ingestClient_, &AnalysisStreamClient::frameReceived, this,
        [this](const AnalysisFramePacket &frame) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (latencyMarkerWriter_ && !firstFrameMarkerWritten_) {
                latencyMarkerWriter_->writeEvent(QStringLiteral("first_analysis_frame"), nowMs,
                    QJsonObject {
                        {QStringLiteral("camera_id"), frame.cameraId},
                        {QStringLiteral("frame_id"), QString::number(frame.frameId)},
                        {QStringLiteral("pixel_format"), frame.pixelFormat == AnalysisPixelFormat::Nv12
                                ? QStringLiteral("nv12")
                                : QStringLiteral("jpeg")},
                    });
                firstFrameMarkerWritten_ = true;
            }

            // existing infer/tracking/classification path remains

            if (latencyMarkerWriter_ && !firstClassificationMarkerWritten_ && !batch.results.isEmpty()) {
                latencyMarkerWriter_->writeEvent(QStringLiteral("first_classification"), runtimeStatus_.lastInferTs,
                    QJsonObject {
                        {QStringLiteral("camera_id"), batch.cameraId},
                        {QStringLiteral("state"), batch.results.first().state},
                        {QStringLiteral("confidence"), batch.results.first().confidence},
                    });
                firstClassificationMarkerWritten_ = true;
            }
        });
}
```

Also add matching members to `fall_daemon_app.h`:

```cpp
std::unique_ptr<LatencyMarkerWriter> latencyMarkerWriter_;
bool firstFrameMarkerWritten_ = false;
bool firstClassificationMarkerWritten_ = false;
```

- [ ] **Step 6: Run the updated tests and verify marker coverage passes**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target video_service_test fall_end_to_end_status_test && \
ctest --test-dir out/build-rk_app-host-tests -R "video_service_test|fall_end_to_end_status_test" --output-on-failure
```

Expected: both tests pass, proving the board-side marker events exist.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_videod/core/video_service.cpp \
        rk_app/src/health_falld/app/fall_daemon_app.cpp \
        rk_app/src/health_falld/app/fall_daemon_app.h \
        rk_app/src/tests/video_daemon_tests/video_service_test.cpp \
        rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
git commit -m "feat: emit rk3588 latency baseline markers"
```

### Task 3: Add a repeatable host-side A/B latency harness for RK3588 bundles

**Files:**
- Create: `deploy/tests/measure_rk3588_test_mode_latency.py`
- Create: `deploy/tests/measure_rk3588_test_mode_latency_test.py`

- [ ] **Step 1: Write the failing parser/computation test for the harness**

```python
import unittest

from deploy.tests.measure_rk3588_test_mode_latency import compute_metrics


class MeasureRk3588TestModeLatencyTest(unittest.TestCase):
    def test_computes_stage_latencies_from_marker_events(self):
        metrics = compute_metrics(
            video_events=[{"event": "playback_started", "ts_ms": 1000}],
            fall_events=[
                {"event": "first_analysis_frame", "ts_ms": 1120},
                {"event": "first_classification", "ts_ms": 1240, "state": "monitoring"},
            ],
        )

        self.assertEqual(metrics["playback_start_ts_ms"], 1000)
        self.assertEqual(metrics["first_frame_ts_ms"], 1120)
        self.assertEqual(metrics["first_classification_ts_ms"], 1240)
        self.assertEqual(metrics["analysis_ingress_latency_ms"], 120)
        self.assertEqual(metrics["classification_stage_latency_ms"], 120)
        self.assertEqual(metrics["startup_classification_latency_ms"], 240)
        self.assertEqual(metrics["first_state"], "monitoring")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the Python test to verify the harness module does not exist yet**

Run:
```bash
python3 -m unittest deploy/tests/measure_rk3588_test_mode_latency_test.py
```

Expected: import failure because `measure_rk3588_test_mode_latency.py` does not exist yet.

- [ ] **Step 3: Implement the minimal measurement harness**

```python
# deploy/tests/measure_rk3588_test_mode_latency.py
#!/usr/bin/env python3
import argparse
import json
import pathlib
import subprocess
import time


def read_jsonl(path: pathlib.Path):
    events = []
    if not path.exists():
        return events
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            events.append(json.loads(line))
    return events


def compute_metrics(video_events, fall_events):
    playback = next(event for event in video_events if event["event"] == "playback_started")
    first_frame = next(event for event in fall_events if event["event"] == "first_analysis_frame")
    first_classification = next(event for event in fall_events if event["event"] == "first_classification")
    return {
        "playback_start_ts_ms": playback["ts_ms"],
        "first_frame_ts_ms": first_frame["ts_ms"],
        "first_classification_ts_ms": first_classification["ts_ms"],
        "analysis_ingress_latency_ms": first_frame["ts_ms"] - playback["ts_ms"],
        "classification_stage_latency_ms": first_classification["ts_ms"] - first_frame["ts_ms"],
        "startup_classification_latency_ms": first_classification["ts_ms"] - playback["ts_ms"],
        "first_state": first_classification.get("state", ""),
    }


def run_ssh(host, password, remote_cmd):
    return subprocess.run(
        ["sshpass", "-p", password, "ssh", "-o", "StrictHostKeyChecking=no", host, remote_cmd],
        check=True,
        capture_output=True,
        text=True,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--bundle-dir", required=True)
    args = parser.parse_args()

    video_marker = "/tmp/rk_video_latency.jsonl"
    fall_marker = "/tmp/rk_fall_latency.jsonl"
    start_cmd = (
        f"cd {args.bundle_dir} && ./scripts/stop.sh >/dev/null 2>&1 || true && "
        f"rm -f {video_marker} {fall_marker} && "
        f"export RK_VIDEO_LATENCY_MARKER_PATH={video_marker} && "
        f"export RK_FALL_LATENCY_MARKER_PATH={fall_marker} && "
        f"RK_RUNTIME_MODE=system ./scripts/start.sh --backend-only"
    )
    run_ssh(args.host, args.password, start_cmd)

    trigger_cmd = (
        "python3 - <<'PY'\n"
        "import json, socket\n"
        f"sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
        f"sock.connect('{args.bundle_dir}/run/rk_video.sock')\n"
        "cmd = {\"action\": \"start_test_input\", \"request_id\": \"latency-1\", \"camera_id\": \"front_cam\", "
        "\"payload\": {\"file_path\": \"/home/elf/Videos/video.mp4\"}}\n"
        "sock.sendall((json.dumps(cmd)+'\\n').encode())\n"
        "print(sock.recv(65535).decode())\n"
        "PY"
    )
    run_ssh(args.host, args.password, trigger_cmd)

    for _ in range(50):
        time.sleep(0.2)
        fetch = run_ssh(args.host, args.password,
            f"python3 - <<'PY'\nimport pathlib\n"
            f"print(pathlib.Path('{video_marker}').read_text() if pathlib.Path('{video_marker}').exists() else '')\n"
            f"print('---SPLIT---')\n"
            f"print(pathlib.Path('{fall_marker}').read_text() if pathlib.Path('{fall_marker}').exists() else '')\nPY")
        raw_video, raw_fall = fetch.stdout.split("---SPLIT---\n", 1)
        video_events = [json.loads(line) for line in raw_video.splitlines() if line.strip()]
        fall_events = [json.loads(line) for line in raw_fall.splitlines() if line.strip()]
        if any(event.get("event") == "first_classification" for event in fall_events):
            print(json.dumps(compute_metrics(video_events, fall_events), indent=2, sort_keys=True))
            return

    raise SystemExit("timed out waiting for first classification marker")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the Python unit test to verify the metric logic passes**

Run:
```bash
python3 -m unittest deploy/tests/measure_rk3588_test_mode_latency_test.py
```

Expected: test passes.

- [ ] **Step 5: Commit**

```bash
git add deploy/tests/measure_rk3588_test_mode_latency.py \
        deploy/tests/measure_rk3588_test_mode_latency_test.py
git commit -m "test: add rk3588 test-mode latency harness"
```

### Task 4: Build `main` and worktree bundles, run the A/B latency comparison, and record the regression boundary

**Files:**
- Create: `docs/testing/2026-04-23-rk3588-test-mode-latency-baseline.md`

- [ ] **Step 1: Build the baseline `main` bundle from the main checkout**

Run from `/home/elf/workspace/QTtest/Qt例程源码/rk_health_station`:
```bash
BUILD_DIR=/tmp/rk_health_station-build-rk3588-main-latency \
BUNDLE_DIR=$PWD/out/rk3588_bundle_latency_main \
bash deploy/scripts/build_rk3588_bundle.sh
```

Expected: ARM64 bundle is produced at `out/rk3588_bundle_latency_main`.

- [ ] **Step 2: Build the candidate worktree bundle from the current worktree**

Run from `/home/elf/workspace/QTtest/Qt例程源码/rk_health_station/.worktrees/feature-rk3588-analysis-pipeline-optimization`:
```bash
BUILD_DIR=/tmp/rk_health_station-build-rk3588-candidate-latency \
BUNDLE_DIR=$PWD/out/rk3588_bundle_latency_candidate \
bash deploy/scripts/build_rk3588_bundle.sh
```

Expected: ARM64 bundle is produced at `out/rk3588_bundle_latency_candidate`.

- [ ] **Step 3: Sync both bundles to separate directories on the RK3588 board**

Run from the main checkout:
```bash
rsync -av --delete out/rk3588_bundle_latency_main/ \
  elf@192.168.137.179:/home/elf/rk3588_bundle_main/
```

Run from the worktree:
```bash
rsync -av --delete out/rk3588_bundle_latency_candidate/ \
  elf@192.168.137.179:/home/elf/rk3588_bundle_candidate/
```

Expected: both bundle directories exist on the board without overwriting each other.

- [ ] **Step 4: Run the latency harness against `main` and save the result**

Run:
```bash
python3 deploy/tests/measure_rk3588_test_mode_latency.py \
  --host elf@192.168.137.179 \
  --password elf \
  --bundle-dir /home/elf/rk3588_bundle_main
```

Expected: JSON output containing at least:
- `startup_classification_latency_ms`
- `analysis_ingress_latency_ms`
- `classification_stage_latency_ms`
- `first_state`

- [ ] **Step 5: Run the latency harness against the candidate worktree and save the result**

Run:
```bash
python3 deploy/tests/measure_rk3588_test_mode_latency.py \
  --host elf@192.168.137.179 \
  --password elf \
  --bundle-dir /home/elf/rk3588_bundle_candidate
```

Expected: JSON output with the same fields as the baseline run.

- [ ] **Step 6: Write the measured comparison result into `docs/testing/2026-04-23-rk3588-test-mode-latency-baseline.md`**

```markdown
# RK3588 Test-Mode Latency Baseline

Date: 2026-04-23
Input file: `/home/elf/Videos/video.mp4`
Board: `192.168.137.179`

## Main Baseline

```json
{
  "analysis_ingress_latency_ms": 0,
  "classification_stage_latency_ms": 0,
  "first_state": "",
  "first_classification_ts_ms": 0,
  "first_frame_ts_ms": 0,
  "playback_start_ts_ms": 0,
  "startup_classification_latency_ms": 0
}
```

## Worktree Candidate

```json
{
  "analysis_ingress_latency_ms": 0,
  "classification_stage_latency_ms": 0,
  "first_state": "",
  "first_classification_ts_ms": 0,
  "first_frame_ts_ms": 0,
  "playback_start_ts_ms": 0,
  "startup_classification_latency_ms": 0
}
```

## Comparison

- `startup_classification_latency_ms`: worktree - main = `...`
- `analysis_ingress_latency_ms`: worktree - main = `...`
- `classification_stage_latency_ms`: worktree - main = `...`

## Root-Cause Boundary

- If the worktree is slower before first analysis ingress, the regression boundary is `health-videod`.
- If the worktree is slower after first analysis ingress, the regression boundary is `health-falld`.
- If both are slower, the regression is cross-service.
```

Replace the placeholder numeric values with the actual measured JSON before committing.

- [ ] **Step 7: Commit**

```bash
git add docs/testing/2026-04-23-rk3588-test-mode-latency-baseline.md
git commit -m "docs: record rk3588 test-mode latency baseline"
```

### Task 5: If the candidate is worse than `main`, implement only the minimal fix at the measured regression boundary

**Files:**
- Modify only the files proven necessary by the Task 4 boundary result.

- [ ] **Step 1: Translate the Task 4 comparison into a single root-cause hypothesis**

Write down one hypothesis in the commit message scratchpad or notes before editing code, for example:

```text
I think the regression is in health-falld startup classification because analysis ingress matches main but the first classification stage is slower by 900 ms.
```

- [ ] **Step 2: Write one failing regression test that encodes the measured boundary behavior**

Examples:

- if the boundary is `health-videod`, add a test proving the first analysis frame is not unnecessarily delayed after test playback start
- if the boundary is `health-falld`, add a test proving the first visible-person classification is emitted under the startup conditions that `main` already satisfies

Use the nearest existing suite:
- `rk_app/src/tests/video_daemon_tests/video_service_test.cpp`
- `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`
- `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
- `rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp`

- [ ] **Step 3: Run the failing test and verify it fails for the expected reason**

Run only the narrowest target that contains the new regression test.

Expected: the new test fails because the measured regression behavior still exists.

- [ ] **Step 4: Implement the smallest fix that makes the candidate no worse than `main`**

Do not bundle speculative improvements. Fix only the measured regression boundary.

- [ ] **Step 5: Re-run the narrow regression test plus the Task 2/Task 3 verification commands**

Run:
```bash
python3 -m unittest deploy/tests/measure_rk3588_test_mode_latency_test.py
cmake --build out/build-rk_app-host-tests -j4 --target \
  latency_marker_writer_test \
  video_service_test \
  fall_end_to_end_status_test && \
ctest --test-dir out/build-rk_app-host-tests -R \
  "latency_marker_writer_test|video_service_test|fall_end_to_end_status_test" \
  --output-on-failure
```

Expected: all targeted checks pass.

- [ ] **Step 6: Rebuild, redeploy, and rerun the board harness for `main` and the candidate**

Use the same commands from Task 4 Steps 1-5.

Expected: the candidate now satisfies:

```text
startup_classification_latency_ms(candidate) <= startup_classification_latency_ms(main)
```

- [ ] **Step 7: Update the baseline note with the post-fix result and commit**

```bash
git add docs/testing/2026-04-23-rk3588-test-mode-latency-baseline.md
git add <only the files actually changed for the measured fix>
git commit -m "fix: restore rk3588 test-mode startup latency parity"
```

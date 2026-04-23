# RK3588 Analysis Pipeline GStreamer RGB Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the fall-analysis preprocessing workload from `health-falld` CPU-side NV12 conversion into the `health-videod` GStreamer analysis branch, while keeping the UI preview chain unchanged.

**Architecture:** Keep the existing MJPEG preview path for UI consumers. Add a production analysis path that emits fixed-size RGB frames through the existing analysis socket, then add a thin RGB fast-path in `health-falld` so pose inference no longer depends on the slow `NV12 -> RGB + resize + letterbox` preprocessing path.

**Tech Stack:** Qt/C++17, GStreamer pipeline composition, existing analysis IPC protocol, RKNN pose runtime, Qt Test/CTest, RK3588 board-side validation.

---

## File Structure Map

### Analysis pixel-format contract
- Modify: `rk_app/src/shared/protocol/analysis_stream_protocol.cpp` - add explicit RGB packet validation support.
- Modify: `rk_app/src/shared/protocol/analysis_stream_protocol.h` - add RGB enum if not already present and keep protocol declarations aligned.
- Modify: `rk_app/src/tests/protocol_tests/analysis_stream_protocol_test.cpp` - add protocol tests for RGB payload size validation.

### Video daemon analysis branch
- Modify: `rk_app/src/health_videod/analysis/analysis_output_backend.h` - keep interface aligned with RGB frame publication.
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h` - reflect RGB publication expectations.
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp` - publish RGB analysis packets and status metadata.
- Modify: `rk_app/src/health_videod/pipeline/video_pipeline_backend.h` - keep analysis-source injection contract stable.
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h` - track RGB analysis branch configuration.
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp` - make the analysis branch output fixed-size RGB raw frames while leaving preview MJPEG untouched.
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp` - verify RGB analysis caps are requested without affecting preview.
- Modify: `rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp` - verify RGB packets are accepted and forwarded correctly.
- Modify: `rk_app/src/tests/video_daemon_tests/video_service_analysis_test.cpp` - verify service-level analysis metadata reflects RGB mode.

### Fall daemon RGB fast-path
- Modify: `rk_app/src/health_falld/pose/rknn_pose_estimator.h` - keep pose-estimator interface aligned with RGB fast-path handling.
- Modify: `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp` - add RGB fast-path and bypass default NV12 preprocessing in the production path.
- Modify: `rk_app/src/health_falld/pose/nv12_preprocessor.cpp` - retain only fallback/compat behavior, if still referenced.
- Modify: `rk_app/src/health_falld/pose/nv12_preprocessor.h` - keep fallback declarations aligned.
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_runtime_pose_stub_test.cpp` - verify RGB frames no longer require NV12 preprocessing.
- Modify: `rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp` - verify RGB frame payloads decode and deliver correctly.
- Create or modify: `rk_app/src/tests/fall_daemon_tests/rgb_pose_fast_path_test.cpp` - focused RGB fast-path regression coverage if the existing tests become too awkward.

### Board-side validation and reporting
- Modify: `deploy/tests/measure_rk3588_test_mode_latency.py` - keep harness compatible with RGB analysis packets and pose timing markers.
- Create: `docs/testing/2026-04-23-rk3588-analysis-pipeline-gstreamer-rgb-results.md` - record real-board before/after timing and acceptance result.

## Task 1: Extend the analysis frame protocol to support strict RGB payload validation

**Files:**
- Modify: `rk_app/src/shared/protocol/analysis_stream_protocol.h`
- Modify: `rk_app/src/shared/protocol/analysis_stream_protocol.cpp`
- Modify: `rk_app/src/tests/protocol_tests/analysis_stream_protocol_test.cpp`

- [ ] **Step 1: Write the failing RGB protocol test**

Add this test to `rk_app/src/tests/protocol_tests/analysis_stream_protocol_test.cpp`:

```cpp
void AnalysisStreamProtocolTest::rejectsRgbFrameWithWrongPayloadSize() {
    AnalysisFramePacket packet;
    packet.frameId = 1;
    packet.timestampMs = 123;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 4;
    packet.height = 4;
    packet.pixelFormat = AnalysisPixelFormat::Rgb;
    packet.payload = QByteArray(4 * 4 * 3 - 1, '\0');

    QByteArray encoded = encodeAnalysisFramePacket(packet);

    QByteArray buffer = encoded;
    AnalysisFramePacket decoded;
    QVERIFY(!takeFirstAnalysisFramePacket(&buffer, &decoded));
}
```

- [ ] **Step 2: Run the focused test to verify it fails**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target analysis_stream_protocol_test && \
ctest --test-dir out/build-rk_app-host-tests -R analysis_stream_protocol_test --output-on-failure
```

Expected: failure because `Rgb` is not yet validated correctly.

- [ ] **Step 3: Implement minimal RGB validation support**

Update the protocol so RGB packets are accepted only when:

```cpp
const qint64 expectedRgbBytes = static_cast<qint64>(packet.width) * packet.height * 3;
```

and rejected otherwise.

- [ ] **Step 4: Re-run the focused test to verify it passes**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target analysis_stream_protocol_test && \
ctest --test-dir out/build-rk_app-host-tests -R analysis_stream_protocol_test --output-on-failure
```

Expected: `analysis_stream_protocol_test` passes.

- [ ] **Step 5: Commit**

```bash
git add rk_app/src/shared/protocol/analysis_stream_protocol.h \
        rk_app/src/shared/protocol/analysis_stream_protocol.cpp \
        rk_app/src/tests/protocol_tests/analysis_stream_protocol_test.cpp
git commit -m "test: add rgb analysis frame protocol validation"
```

## Task 2: Change the video-daemon analysis branch to emit fixed-size RGB frames without touching preview MJPEG

**Files:**
- Modify: `rk_app/src/health_videod/analysis/analysis_output_backend.h`
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h`
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`
- Modify: `rk_app/src/health_videod/pipeline/video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/video_service_analysis_test.cpp`

- [ ] **Step 1: Write the failing pipeline test for RGB analysis caps**

Add a focused test in `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp` asserting the analysis branch requests RGB output while preview remains MJPEG. The assertion should require these substrings in the generated command:

```cpp
QVERIFY(arguments.contains(QStringLiteral("videoconvert")));
QVERIFY(arguments.contains(QStringLiteral("videoscale")));
QVERIFY(arguments.contains(QStringLiteral("video/x-raw,format=RGB")));
QVERIFY(arguments.contains(QStringLiteral("fdsink fd=1")));
QVERIFY(arguments.contains(QStringLiteral("jpegenc")));
QVERIFY(arguments.contains(QStringLiteral("multipartmux")));
```

- [ ] **Step 2: Run the focused pipeline test to verify it fails**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target gstreamer_video_pipeline_backend_test && \
ctest --test-dir out/build-rk_app-host-tests -R gstreamer_video_pipeline_backend_test --output-on-failure
```

Expected: failure because the analysis branch still emits NV12.

- [ ] **Step 3: Change the GStreamer analysis branch to fixed-size RGB raw**

Update `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp` so the analysis tap uses GStreamer to produce fixed-size RGB raw frames. Keep the preview branch untouched. The analysis fragment should conceptually follow this shape:

```cpp
" t. ! queue leaky=downstream max-size-buffers=1 ! "
"videorate drop-only=true ! "
"videoconvert ! videoscale ! "
"video/x-raw,format=RGB,width=%1,height=%2,framerate=%3/1 ! "
"fdsink fd=1 sync=false"
```

The published `AnalysisFramePacket` must then use:

```cpp
packet.pixelFormat = AnalysisPixelFormat::Rgb;
packet.payload.size() == width * height * 3;
```

- [ ] **Step 4: Update the analysis output backend tests for RGB packet forwarding**

Adjust or add a test in `rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp` so it expects `pixel_format = rgb` and `outputFormat = "rgb"` for the production analysis path.

- [ ] **Step 5: Re-run the narrow video-daemon tests**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target \
  gstreamer_video_pipeline_backend_test \
  analysis_output_backend_test \
  video_service_analysis_test && \
ctest --test-dir out/build-rk_app-host-tests -R \
  "gstreamer_video_pipeline_backend_test|analysis_output_backend_test|video_service_analysis_test" \
  --output-on-failure
```

Expected: all three tests pass.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/health_videod/analysis/analysis_output_backend.h \
        rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h \
        rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp \
        rk_app/src/health_videod/pipeline/video_pipeline_backend.h \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp \
        rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp \
        rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp \
        rk_app/src/tests/video_daemon_tests/video_service_analysis_test.cpp
git commit -m "feat: emit rgb analysis frames from videod"
```

## Task 3: Add a thin RGB fast-path in the pose estimator and bypass default NV12 preprocessing

**Files:**
- Modify: `rk_app/src/health_falld/pose/rknn_pose_estimator.h`
- Modify: `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp`
- Modify: `rk_app/src/health_falld/pose/nv12_preprocessor.cpp`
- Modify: `rk_app/src/health_falld/pose/nv12_preprocessor.h`
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_runtime_pose_stub_test.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp`
- Create or modify: `rk_app/src/tests/fall_daemon_tests/rgb_pose_fast_path_test.cpp`

- [ ] **Step 1: Write the failing RGB fast-path test**

Create or extend a fall-daemon pose test so that a fixed-size RGB `AnalysisFramePacket` reaches the pose estimator without invoking NV12 preprocessing. The test should construct:

```cpp
AnalysisFramePacket frame;
frame.cameraId = QStringLiteral("front_cam");
frame.width = 640;
frame.height = 640;
frame.pixelFormat = AnalysisPixelFormat::Rgb;
frame.payload = QByteArray(640 * 640 * 3, '\0');
```

and expect the pose-estimator path to accept it as a valid model input shape.

- [ ] **Step 2: Run the focused fall-daemon test to verify it fails**

Run the smallest relevant target, for example:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target fall_runtime_pose_stub_test && \
ctest --test-dir out/build-rk_app-host-tests -R fall_runtime_pose_stub_test --output-on-failure
```

Expected: failure because RGB fast-path support is missing.

- [ ] **Step 3: Implement the RGB fast-path in `rknn_pose_estimator.cpp`**

Add a direct branch for RGB frames so production-path RGB payloads do not go through NV12 preprocessing. The fast-path should validate:

```cpp
frame.pixelFormat == AnalysisPixelFormat::Rgb
frame.payload.size() == targetWidth * targetHeight * 3
frame.width == targetWidth
frame.height == targetHeight
```

Then inject the payload directly into RKNN input:

```cpp
input.type = RKNN_TENSOR_UINT8;
input.fmt = RKNN_TENSOR_NHWC;
input.size = frame.payload.size();
input.buf = const_cast<char *>(frame.payload.constData());
```

Keep the JPEG path for baseline compatibility. Keep the NV12 path only as fallback/diagnostic behavior, not the intended default production path.

- [ ] **Step 4: Re-run the narrow fall-daemon tests**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target \
  fall_runtime_pose_stub_test \
  analysis_stream_client_test \
  nv12_preprocessor_test && \
ctest --test-dir out/build-rk_app-host-tests -R \
  "fall_runtime_pose_stub_test|analysis_stream_client_test|nv12_preprocessor_test" \
  --output-on-failure
```

Expected: targeted tests pass.

- [ ] **Step 5: Commit**

```bash
git add rk_app/src/health_falld/pose/rknn_pose_estimator.h \
        rk_app/src/health_falld/pose/rknn_pose_estimator.cpp \
        rk_app/src/health_falld/pose/nv12_preprocessor.cpp \
        rk_app/src/health_falld/pose/nv12_preprocessor.h \
        rk_app/src/tests/fall_daemon_tests/fall_runtime_pose_stub_test.cpp \
        rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp \
        rk_app/src/tests/fall_daemon_tests/rgb_pose_fast_path_test.cpp
git commit -m "feat: add rgb pose fast path"
```

## Task 4: Re-run pose-stage instrumentation locally and verify the expected bottleneck shift before board deployment

**Files:**
- Reuse the already-added pose timing instrumentation in:
  - `rk_app/src/health_falld/pose/pose_stage_timing_logger.h`
  - `rk_app/src/health_falld/pose/pose_stage_timing_logger.cpp`
  - `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp`

- [ ] **Step 1: Re-run the timing logger and nearby unit tests**

Run:
```bash
cmake --build out/build-rk_app-host-tests -j4 --target \
  pose_stage_timing_logger_test \
  fall_runtime_pose_stub_test \
  gstreamer_video_pipeline_backend_test \
  analysis_stream_protocol_test && \
ctest --test-dir out/build-rk_app-host-tests -R \
  "pose_stage_timing_logger_test|fall_runtime_pose_stub_test|gstreamer_video_pipeline_backend_test|analysis_stream_protocol_test" \
  --output-on-failure
```

Expected: all targeted tests pass.

- [ ] **Step 2: Commit any timing-related support adjustments if needed**

```bash
git add rk_app/src/health_falld/pose/pose_stage_timing_logger.h \
        rk_app/src/health_falld/pose/pose_stage_timing_logger.cpp \
        rk_app/src/health_falld/pose/rknn_pose_estimator.cpp \
        rk_app/src/tests/fall_daemon_tests/pose_stage_timing_logger_test.cpp
git commit -m "test: support rgb pose timing validation"
```

If no additional changes were required beyond already-staged work, skip this commit.

## Task 5: Build, deploy, and compare real-board `main` versus candidate using the same pose timing breakdown

**Files:**
- Modify: `deploy/tests/measure_rk3588_test_mode_latency.py` only if needed for compatibility
- Create: `docs/testing/2026-04-23-rk3588-analysis-pipeline-gstreamer-rgb-results.md`

- [ ] **Step 1: Build the temporary main measurement bundle**

Run from `/tmp/rk3588_main_latency_baseline`:
```bash
BUILD_DIR=/tmp/rk3588_main_rgb_analysis_build \
BUNDLE_DIR=$PWD/out/rk3588_bundle_latency_main \
bash deploy/scripts/build_rk3588_bundle.sh
```

- [ ] **Step 2: Build the candidate bundle**

Run from `/home/elf/workspace/QTtest/Qt例程源码/rk_health_station/.worktrees/feature-rk3588-analysis-pipeline-optimization`:
```bash
BUILD_DIR=/tmp/rk3588_candidate_rgb_analysis_build \
BUNDLE_DIR=$PWD/out/rk3588_bundle_latency_candidate \
bash deploy/scripts/build_rk3588_bundle.sh
```

- [ ] **Step 3: Sync refreshed minimal bundles to the board**

Refresh the minimal bundle directories and sync them to:
- `/home/elf/rk3588_bundle_main`
- `/home/elf/rk3588_bundle_candidate`

Use the same minimal-bundle strategy already established in this worktree session.

- [ ] **Step 4: Run the temporary main board measurement with pose timing enabled**

Start the temporary main bundle with:
- `RK_FALL_POSE_TIMING_PATH=/tmp/mainpose_pose.jsonl`
- `RK_VIDEO_LATENCY_MARKER_PATH=/tmp/mainpose_video_latency.jsonl`
- `RK_FALL_LATENCY_MARKER_PATH=/tmp/mainpose_fall_latency.jsonl`
- `RK_FALL_TRACK_TRACE_PATH=/tmp/mainpose_trace.jsonl`

Then trigger:
- `start_test_input`
- `/home/elf/Videos/video.mp4`

Fetch and summarize:
- `preprocess_ms`
- `inputs_set_ms`
- `rknn_run_ms`
- `outputs_get_ms`
- `post_process_ms`
- `total_ms`
- first-classification marker timestamp

- [ ] **Step 5: Run the candidate board measurement with the same instrumentation**

Use the same flow and same metrics with candidate-specific marker paths.

- [ ] **Step 6: Write the board result note**

Create `docs/testing/2026-04-23-rk3588-analysis-pipeline-gstreamer-rgb-results.md` with this structure:

```markdown
# RK3588 Analysis Pipeline GStreamer RGB Results

Date: 2026-04-23
Board: `192.168.137.179`
Input: `/home/elf/Videos/video.mp4`

## Main Baseline
- startup classification latency: `... ms`
- preprocess avg: `... ms`
- rknn_run avg: `... ms`
- total avg: `... ms`

## Candidate RGB Analysis Path
- startup classification latency: `... ms`
- preprocess avg: `... ms`
- rknn_run avg: `... ms`
- total avg: `... ms`

## Comparison
- preprocess delta: `candidate - main = ... ms`
- total pose delta: `candidate - main = ... ms`
- startup classification delta: `candidate - main = ... ms`

## Result
- `PASS` if candidate startup classification latency is no worse than main
- otherwise `FAIL`, with the remaining bottleneck explicitly named
```

- [ ] **Step 7: Commit**

```bash
git add docs/testing/2026-04-23-rk3588-analysis-pipeline-gstreamer-rgb-results.md
if git diff --cached --quiet; then
  echo "No result note changes to commit"
else
  git commit -m "docs: record gstreamer rgb analysis path results"
fi
```

## Self-Review

- Spec coverage: the plan covers protocol validation, video-daemon RGB emission, fall-daemon RGB fast-path consumption, local test verification, and real-board comparison against temporary `main`.
- Placeholder scan: no `TODO`/`TBD` placeholders remain; every task names exact files, commands, and expected outcomes.
- Type consistency: the plan consistently refers to RGB analysis packets, `AnalysisFramePacket`, and the pose timing fields `preprocess_ms`, `inputs_set_ms`, `rknn_run_ms`, `outputs_get_ms`, `post_process_ms`, and `total_ms`.

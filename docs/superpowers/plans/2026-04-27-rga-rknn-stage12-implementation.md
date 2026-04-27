# RGA + RKNN Stage 1/2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Scheme B performance optimization using RGA model-input letterbox metadata and RKNN preallocated output buffers, then compare against the captured `main` baseline.

**Architecture:** Keep the current `health-videod -> shared memory ring -> health-falld` topology. Extend analysis frame metadata, have RGA create model-ready RGB frames with letterbox metadata, and have RKNN pose inference use reusable output buffers with legacy fallback; keep full IO memory as an opt-in board probe.

**Tech Stack:** C++17, Qt 5.15, CMake/CTest, Rockchip RGA `im2d`, RKNN rknpu2 API, RK3588 bundle scripts.

---

### Task 1: Add Pose Preprocess Metadata To Analysis Frames

**Files:**
- Modify: `rk_app/src/shared/models/fall_models.h`
- Modify: `rk_app/src/shared/protocol/analysis_frame_descriptor_protocol.cpp`
- Modify: `rk_app/src/health_videod/analysis/shared_memory_frame_ring.h`
- Modify: `rk_app/src/health_videod/analysis/shared_memory_frame_ring.cpp`
- Modify: `rk_app/src/health_falld/ingest/shared_memory_frame_reader.cpp`
- Test: `rk_app/src/tests/protocol_tests/analysis_frame_descriptor_protocol_test.cpp`
- Test: `rk_app/src/tests/video_daemon_tests/shared_memory_frame_ring_test.cpp`

- [ ] Write failing protocol/shared-memory tests for metadata round-trip.
- [ ] Run `cmake --build out/build-rk_app-host --target analysis_frame_descriptor_protocol_test shared_memory_frame_ring_test -j4` and focused `ctest`; expect metadata assertions to fail before implementation.
- [ ] Add `posePreprocessed`, `poseXPad`, `poseYPad`, and `poseScale` fields to packet and descriptor with safe defaults.
- [ ] Encode/decode descriptor version 2 while accepting version 1 as default metadata.
- [ ] Store/load metadata in shared-memory slot headers.
- [ ] Run focused tests until green.

### Task 2: Letterbox In RGA Converter And Publish Metadata

**Files:**
- Modify: `rk_app/src/health_videod/analysis/analysis_frame_converter.h`
- Modify: `rk_app/src/health_videod/analysis/rga_frame_converter.h`
- Modify: `rk_app/src/health_videod/analysis/rga_frame_converter.cpp`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Test: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`

- [ ] Write a failing test using the fake converter to return metadata and verify the descriptor preserves it.
- [ ] Update converter interface to optionally return pose metadata.
- [ ] In the RGA implementation, fill the destination canvas with 114 and resize NV12 into the computed letterbox ROI. Return `posePreprocessed=true`, `xPad`, `yPad`, and `scale`.
- [ ] In the pipeline, copy converter metadata into `AnalysisFramePacket` and `AnalysisFrameDescriptor`.
- [ ] Run focused video-daemon tests until green.

### Task 3: Consume Model-Ready RGB Metadata In Pose Estimator

**Files:**
- Modify: `rk_app/src/health_falld/pose/nv12_preprocessor.h`
- Modify: `rk_app/src/health_falld/pose/nv12_preprocessor.cpp`
- Modify: `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp`
- Test: `rk_app/src/tests/fall_daemon_tests/nv12_preprocessor_test.cpp`
- Test: `rk_app/src/tests/fall_daemon_tests/rgb_pose_fast_path_test.cpp`

- [ ] Write failing tests showing a model-sized RGB packet marked pose-preprocessed returns the supplied metadata without copying/letterboxing.
- [ ] Add helper logic so the RGB fast path uses packet metadata when present; keep old defaults for unmarked packets.
- [ ] Update `RknnPoseEstimator` fast path to use metadata for `letterbox_t`.
- [ ] Run focused fall-daemon preprocessing tests until green.

### Task 4: Add RKNN Output Preallocation / IO-Memory Probe With Fallback

**Files:**
- Modify: `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp`
- Modify: `rk_app/src/health_falld/pose/pose_stage_timing_logger.h`
- Modify: `rk_app/src/health_falld/pose/pose_stage_timing_logger.cpp`
- Test: host coverage is limited because real RKNN code is behind `RKAPP_ENABLE_REAL_RKNN_POSE`; verify through RK3588 cross-build and board timing.

- [x] Add runtime members for full IO-memory probing (`inputMem`, `outputMems`, `ioMemReady`) and default output preallocation (`outputBuffers`, `outputPreallocReady`).
- [x] During model load, preallocate reusable RKNN output buffers by default; allocate/bind input/output mem only when `RK_FALL_RKNN_IO_MEM=zero_copy` or `full`.
- [x] During infer, pass preallocated output buffers to `rknn_outputs_get`; in opt-in IO-memory mode, copy input payload to input mem, call `rknn_run`, build temporary `rknn_output` views over output mem, and skip `rknn_outputs_get/release`.
- [x] Keep legacy `rknn_inputs_set` / allocator-owned `rknn_outputs_get` path if optimization setup fails or is disabled by env.
- [x] Extend timing logs with `io_mem_path` and `output_prealloc_path` so board comparison is unambiguous.

### Task 5: Full Verification And Board Comparison

**Files:**
- Modify: `docs/DevelopmentLog/2026-04-27-rga-rknn-stage12-result.md`

- [ ] Run deploy script tests.
- [ ] Run `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON`.
- [ ] Run `QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host --output-on-failure`.
- [ ] Run `BUILD_DIR=/tmp/rk_health_station-build-rk3588-current bash deploy/scripts/build_rk3588_bundle.sh`.
- [ ] Upload updated bundle pieces to `/home/elf/rk3588_bundle`.
- [ ] Start board with `RK_FALL_POSE_TIMING_PATH=/tmp/rk_pose_timing_stage12.jsonl ./scripts/start_all.sh` and collect a 30-45s timing window.
- [ ] Compare `video_perf`, `fall_perf`, process CPU, and pose timing against the baseline in the design doc.
- [ ] Document whether Scheme B produced the required performance improvement and any remaining third-stage DMABUF work.

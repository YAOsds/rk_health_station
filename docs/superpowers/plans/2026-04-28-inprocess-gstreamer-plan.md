# In-Process GStreamer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an optional in-process GStreamer preview+analysis appsink path for `health-videod` while preserving the current `gst-launch` fallback.

**Architecture:** `GstreamerVideoPipelineBackend` keeps ownership of public video backend behavior and chooses either the existing external process path or a new focused `InProcessGstreamerPipeline` helper. The helper is compiled only when `RKAPP_ENABLE_INPROCESS_GSTREAMER=ON`; host builds without GStreamer dev packages still build and test the default path.

**Tech Stack:** C++17, Qt 5/6 Core, GStreamer 1.22 app/video on RK3588, existing DMABUF descriptor transport.

---

### Task 1: Backend Selection Guard

**Files:**
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Test: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`

- [ ] Add a unit test that sets `RK_VIDEO_PIPELINE_BACKEND=inproc_gst` on a host build without GStreamer support and expects `startPreview()` to fail with `inprocess_gstreamer_not_built`.
- [ ] Implement `inProcessGstreamerRequested()` and compile-time guard so the test passes.
- [ ] Run `cmake --build out/build-rk_app-host --target gstreamer_video_pipeline_backend_test -j4` and `QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R gstreamer_video_pipeline_backend_test --output-on-failure`.

### Task 2: Optional GStreamer Build Wiring

**Files:**
- Modify: `rk_app/CMakeLists.txt`
- Modify: `rk_app/src/health_videod/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Modify: `deploy/scripts/build_rk3588_bundle.sh`

- [ ] Add `RKAPP_ENABLE_INPROCESS_GSTREAMER` CMake option default `OFF`.
- [ ] When enabled, use `pkg_check_modules` for `gstreamer-1.0`, `gstreamer-app-1.0`, and `gstreamer-video-1.0`.
- [ ] Link only `health-videod` to GStreamer; host test targets stay independent unless explicitly enabled.
- [ ] Pass `-DRKAPP_ENABLE_INPROCESS_GSTREAMER=ON` from the RK3588 bundle script.

### Task 3: In-Process Preview+Analysis Helper

**Files:**
- Create: `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.h`
- Create: `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.cpp`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`

- [ ] Implement a helper that starts an equivalent preview pipeline with `appsink name=analysis_sink` for analysis frames.
- [ ] On appsink samples, map the `GstBuffer` to a `QByteArray` and call a callback supplied by `GstreamerVideoPipelineBackend`.
- [ ] Forward bus errors/EOS to the existing observer error/playback handling.
- [ ] Ensure stop sets pipeline state to NULL and joins the GLib loop thread.

### Task 4: Reuse Existing Analysis Publishing

**Files:**
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`

- [ ] Extract the current stdout frame handling after `inputFrame` creation into a reusable method like `processAnalysisFrameBytes(cameraId, inputFrame)`.
- [ ] Call that method from both stdout handling and the new appsink callback.
- [ ] Preserve existing RGA conversion, DMABUF publication, shared-memory fallback, logs, and latency markers.

### Task 5: Verification and Board Smoke

**Files:**
- No source files unless fixes are required.

- [ ] Run host full test suite.
- [ ] Cross-build RK3588 bundle.
- [ ] Deploy to board.
- [ ] Start with `RK_VIDEO_PIPELINE_BACKEND=inproc_gst RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf RK_VIDEO_ANALYSIS_DMA_HEAP=/dev/dma_heap/system-uncached-dma32 RK_FALL_RKNN_INPUT_DMABUF=1`.
- [ ] Verify `status.sh`, `video_perf`, `fall_perf`, DMABUF latency markers, `input_dmabuf_path=true`, and no fatal/crash logs.

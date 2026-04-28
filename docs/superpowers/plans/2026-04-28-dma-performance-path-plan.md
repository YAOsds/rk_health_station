# DMA Performance Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce video analysis CPU memory copies by letting RGA write model-ready RGB frames directly into DMA heap buffers and publishing those fds.

**Architecture:** Add an optional DMA-output conversion interface to `AnalysisFrameConverter`, implement it in `RgaFrameConverter`, and have `GstreamerVideoPipelineBackend` use it when `RK_VIDEO_RGA_OUTPUT_DMABUF=1` and the analysis consumer supports DMABUF. Preserve the existing QByteArray path as fallback.

**Tech Stack:** C++17, Qt, RK RGA `im2d`, Linux DMA heap, existing analysis descriptor fd transport, CMake/CTest, RK3588 board validation.

---

### Task 1: Converter DMA Output Contract

**Files:**
- Modify: `rk_app/src/health_videod/analysis/analysis_frame_converter.h`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`

- [ ] Add a small `AnalysisDmaBuffer` result type with fd, payload bytes, stride, and metadata.
- [ ] Add virtual `convertNv12ToRgbDma(...)` with a default false implementation so host tests and existing fake converters still build.
- [ ] Write a failing test that enables `RK_VIDEO_RGA_OUTPUT_DMABUF=1`, uses a fake converter returning a memfd-backed RGB frame, and asserts the backend publishes a DMABUF descriptor without calling the QByteArray converter.
- [ ] Run `cmake --build out/build-rk_app-host --target gstreamer_video_pipeline_backend_test -j4` and the focused ctest; expected failure: no DMABUF conversion path yet.

### Task 2: Backend DMA Output Path

**Files:**
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`

- [ ] Add `RK_VIDEO_RGA_OUTPUT_DMABUF` env parsing.
- [ ] In `processAnalysisFrameBytes`, when analysis backend is RGA, DMABUF output is requested, and the analysis source supports DMABUF, call `convertNv12ToRgbDma` first.
- [ ] Publish the returned fd with descriptor fields: RGB, 640x640, payload bytes, one plane, offset, stride.
- [ ] Close the returned fd after `publishDmaBufDescriptor` because the fd sender duplicates it.
- [ ] Fallback to current QByteArray path if DMA output conversion fails.
- [ ] Run the focused host test; expected pass.

### Task 3: RGA DMA Heap Implementation

**Files:**
- Modify: `rk_app/src/health_videod/analysis/rga_frame_converter.cpp`
- Modify: `rk_app/src/health_videod/analysis/rga_frame_converter.h`

- [ ] Allocate DMA heap output with `/dev/dma_heap/system-uncached-dma32` or `RK_VIDEO_ANALYSIS_DMA_HEAP`.
- [ ] Map/fill the output with 114 padding.
- [ ] Use RGA fd output (`importbuffer_fd` / `wrapbuffer_handle`) and virtual NV12 input for Stage 1.
- [ ] Return fd ownership to the caller on success; close on failure.
- [ ] Keep `convertNv12ToRgb` unchanged for fallback.
- [ ] Cross-build with `BUILD_DIR=/tmp/rk_health_station-build-rk3588-dma-output BUILD_TESTING=OFF bash deploy/scripts/build_rk3588_bundle.sh`.

### Task 4: Board Verification

**Files:**
- No source changes expected.

- [ ] Deploy `out/rk3588_bundle` to `/home/elf/rk3588_bundle`.
- [ ] Run the board with `RK_VIDEO_PIPELINE_BACKEND=inproc_gst`, `RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf`, `RK_VIDEO_RGA_OUTPUT_DMABUF=1`, and `RK_FALL_RKNN_INPUT_DMABUF=1`.
- [ ] Verify `video_runtime` logs show DMA output path enabled or fallback reason.
- [ ] Verify latency markers contain `transport=dmabuf` and fall pose timing contains `input_dmabuf_path=true`.
- [ ] Verify `video_perf` and `fall_perf` remain stable and no `gst-launch-1.0` process is running.

# GStreamer DMABUF Input Negotiation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the in-process GStreamer analysis appsink preserve DMABUF input when the RK3588 GStreamer/V4L2 stack can negotiate it, while keeping byte fallback safe.

**Architecture:** First run board probes to identify the smallest pipeline that produces `GstBuffer(memory:DMABuf)` at appsink. Then encode that negotiation in `InprocessGstreamerPipeline` behind existing runtime flags, preserving the current byte path when DMABUF negotiation is unavailable.

**Tech Stack:** C++17, Qt, GStreamer app/video/allocators, RK3588 v4l2src, CMake/CTest, board validation.

---

### Task 1: Board Capability Probe

**Files:**
- Create if needed: `deploy/probes/gst_dmabuf_appsink_probe.cpp`
- Create if needed: `deploy/scripts/build_gst_dmabuf_appsink_probe.sh`

- [ ] Inspect board `v4l2src` `io-mode` support with `gst-inspect-1.0 v4l2src`.
- [ ] Probe minimum pipeline `v4l2src ! video/x-raw,format=NV12 ! appsink` and print whether memory is DMABUF.
- [ ] Probe variants with `io-mode=dmabuf`, `io-mode=dmabuf-import`, and `video/x-raw(memory:DMABuf)`.
- [ ] Add `queue`, `tee`, and `videorate` one at a time to identify which element drops DMABUF.

### Task 2: Main Pipeline Negotiation

**Files:**
- Modify: `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.cpp`
- Modify: `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.h`
- Test: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`

- [ ] Add a focused unit test or launch-string test seam for DMABUF caps/io-mode behavior.
- [ ] Preserve default byte path when no DMABUF memory is available.
- [ ] Enable DMABUF caps only under explicit runtime flag and only for the analysis branch.
- [ ] Keep existing failure fallback: if DMABUF input conversion fails, map the sample and process bytes.

### Task 3: Verification

**Files:**
- Modify: `docs/DevelopmentLog/2026-04-28-dma-inprocess-gstreamer-development-log.md`

- [ ] Run focused host tests and full host CTest.
- [ ] Cross-build RK3588 bundle.
- [ ] Deploy and run on board with `RK_VIDEO_GST_DMABUF_INPUT=1`.
- [ ] Confirm logs either show `gst_dmabuf_input available` with `rga_input_dmabuf=true`, or document the exact board limitation if negotiation still fails.

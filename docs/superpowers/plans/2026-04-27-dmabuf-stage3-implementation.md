# DMABUF Stage 3 Feasibility Implementation Plan

**Goal:** Validate DMA/DMABUF feasibility on the RK3588 board first, then implement only the smallest sidecar probe needed to prove the path before touching production transport.

## Task 1: Board Capability Probe

- [x] Query `/dev/dma_heap`, `/dev/dri`, `/dev/rga`, `/dev/video*`, and permissions.
- [x] Query V4L2 formats and streaming capabilities for the active camera device.
- [x] Query GStreamer plugins/caps related to V4L2, DMABUF, RGA, MPP, and appsink/fdsink.
- [x] Query RKNN runtime/header availability and symbols for `rknn_create_mem_from_fd`.
- [x] Save results to `docs/DevelopmentLog/2026-04-27-dmabuf-stage3-capability-report.md`.

## Task 2: Decide Probe Shape

- [x] If dma-heap and RGA fd wrapping are available, create a sidecar RGA fd probe.
- [x] If RKNN fd import is available, extend the probe to import the fd into RKNN.
- [x] If GStreamer DMABUF export is not practical with current `gst-launch` topology, keep capture out of the first probe and document the production implication.

## Task 3: Sidecar Probe With Tests

- [ ] Add a small host-testable abstraction for probe result parsing/reporting before adding board-specific code.
- [x] Add the board-only probe target or script with guarded build/configuration.
- [x] Keep all production frame transport code unchanged.
- [ ] Run focused host tests after every code change.
- [x] Cross-build and run the probe on RK3588 after every board-relevant code change.

## Task 4: Production Proposal Gate

- [ ] Compare probe timing/correctness against current shared-memory path.
- [ ] Decide whether to implement fd transport in production.
- [ ] If yes, write a separate production transport design before modifying `health-videod` / `health-falld` mainline code.

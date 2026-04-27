# DMABUF Stage 3 Feasibility And Probe Design

## Goal

Evaluate and then introduce DMA/DMABUF acceleration without destabilizing the current `health-videod -> shared memory -> health-falld` path. The first deliverable is a board-verified capability report and a sidecar probe. Mainline frame transport changes are only allowed after the probe proves format, stride, cache sync, fd lifetime, and RKNN compatibility on the RK3588 board.

## Current Baseline

The current `main` already uses RGA to produce pose-model-sized RGB analysis frames and `health-falld` uses those frames without CPU letterboxing. Board timing at 15 FPS shows pose `total_ms` around 23.7 ms, while stable ingest/infer FPS remains 15. The remaining likely performance opportunity is reducing cross-process frame payload movement and CPU/cache pressure, especially before increasing analysis FPS to 25.

## Recommended Approach

Use a three-step rollout:

1. **Capability probe only**: collect board facts for dma-heap devices, V4L2 DMABUF support, GStreamer DMABUF caps, RGA fd-buffer support, and RKNN `rknn_create_mem_from_fd` support. This step makes no production code changes.
2. **Sidecar DMABUF probe**: add a small debug/probe target or script that allocates/imports a DMA buffer, runs a controlled RGA operation against fd-backed memory, optionally imports the fd into RKNN, and reports timing/correctness. This must not replace the current production transport.
3. **Production transport**: after probe evidence, add a fallback-protected fd descriptor path. `health-videod` keeps a DMABUF pool, RGA writes model-ready frames into fd-backed buffers, and `health-falld` receives fds through Unix domain socket `SCM_RIGHTS` or an equivalent side channel. The current shared-memory payload ring remains the fallback.

## Non-Goals For The First Iteration

- Do not replace `SharedMemoryFrameRing` immediately.
- Do not require 25 FPS until the 15 FPS DMABUF probe is stable.
- Do not remove the existing byte-payload fallback.
- Do not couple the probe to UI preview transport.

## Success Criteria

- Board capability report documents available dma-heap nodes, GStreamer/V4L2 support, RGA fd-buffer behavior, and RKNN fd import behavior.
- Sidecar probe runs on RK3588 without crashing and prints enough timing/correctness evidence to compare against the existing path.
- Production code remains unchanged until probe evidence supports it.
- Any eventual production path has an explicit fallback to shared-memory payload transport and can be disabled through environment configuration.

## Risks

- GStreamer `gst-launch + fdsink fd=1` cannot transfer DMABUF file descriptors as ordinary stdout bytes; production fd passing likely requires an in-process GStreamer path or a helper that can pass fds over Unix sockets.
- RGA and RKNN may disagree on required format/stride/layout. RGB888 model input is simple, but fd-backed buffer stride and cache synchronization must still be verified.
- Full RKNN IO-memory was previously neutral or regressive in total pose timing, so importing DMABUF into RKNN must be measured rather than assumed beneficial.
- fd lifetime and pool reuse must be explicit to avoid use-after-close, leaks, or overwriting a frame while `health-falld` is still consuming it.

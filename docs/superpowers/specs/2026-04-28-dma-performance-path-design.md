# DMA Performance Path Design

## Goal

Improve RK3588 video analysis performance by reducing full-frame CPU memory copies and moving the analysis input path toward DMABUF-backed buffers.

## Current State

The in-process GStreamer backend can run without an external `gst-launch` process and publishes analysis descriptors over DMABUF transport. However, `health-videod` still maps/copies appsink frames into `QByteArray`, RGA writes RGB output into `QByteArray`, then `health-videod` copies that RGB payload into a newly allocated DMA heap buffer before publishing the fd to `health-falld`.

## Recommended Architecture

Use a staged DMA path. First, keep appsink input handling unchanged but make RGA write RGB 640x640 output directly into a DMA heap buffer, then publish that fd. This removes the most certain large copy while preserving the existing `health-falld`/RKNN RGB DMABUF fast path. Second, probe and optionally use GstBuffer DMABUF memory as RGA input when the board pipeline exposes it.

## Data Flow

Stage 1 target:

```text
appsink NV12 bytes -> RGA virtual input -> RGA DMA heap RGB output -> descriptor fd -> health-falld -> RKNN fd input
```

Stage 2 target:

```text
appsink GstBuffer DMABUF NV12 -> RGA fd input -> RGA DMA heap RGB output -> descriptor fd -> health-falld -> RKNN fd input
```

## Runtime Controls

- Keep existing default/fallback behavior intact.
- Add `RK_VIDEO_RGA_OUTPUT_DMABUF=1` to enable RGA DMA output.
- Add a later `RK_VIDEO_GST_DMABUF_INPUT=1` or equivalent for GstBuffer DMABUF input after Stage 1 is stable.
- On any DMA/RGA fd failure, fall back to the current `QByteArray` path and log the reason.

## Verification

Each stage must be host-tested where possible, cross-built, deployed to the RK3588 board, and run with:

```bash
RK_VIDEO_PIPELINE_BACKEND=inproc_gst
RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf
RK_VIDEO_RGA_OUTPUT_DMABUF=1
RK_FALL_RKNN_INPUT_DMABUF=1
```

Success requires stable service startup, no external `gst-launch-1.0` process, `transport=dmabuf`, `input_dmabuf_path=true`, and video/fall performance logs with no fatal pipeline errors.

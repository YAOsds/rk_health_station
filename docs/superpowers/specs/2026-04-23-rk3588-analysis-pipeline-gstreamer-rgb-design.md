# RK3588 Analysis Pipeline GStreamer RGB Design

Date: 2026-04-23
Status: Approved design
Scope: RK3588 fall-analysis pipeline only

## Goal

Restore product-grade fall-analysis startup latency by moving the expensive image preprocessing work out of `health-falld` and into the `health-videod` GStreamer analysis branch, while keeping the UI preview pipeline unchanged.

## Non-Goals

- Do not change the UI preview chain or its MJPEG transport behavior.
- Do not merge processes or reduce service isolation between `health-videod` and `health-falld`.
- Do not introduce DMABUF/shared-buffer transport in this version.
- Do not redesign the RKNN pose model itself.

## Problem Statement

The current candidate pipeline replaced the old JPEG analysis path with an NV12 raw path, but real-board measurements show worse startup latency than `main`.

Measured board-side evidence:

- `main` first useful classification: about 1.8s
- current candidate first useful classification: about 4.2s
- `main` pose preprocess: about 5-6 ms/frame
- current candidate pose preprocess: about 60 ms/frame

This proves the main regression is not in LSTM classification and not in RKNN runtime execution itself. The dominant regression is the CPU-side preprocessing now performed in `health-falld`, specifically the `NV12 -> RGB + resize + letterbox` path.

The current candidate architecture gets raw frames earlier, but it does not inject them into YOLO in a model-friendly format. Instead, it forwards NV12 into `health-falld`, where the service performs a slow CPU preprocessing step. As a result, the intended raw-path benefit is lost.

## Design Summary

Adopt a GStreamer-first preprocessing design for the fall-analysis branch:

- `health-videod` keeps the current MJPEG preview branch for the UI.
- `health-videod` adds or refines a dedicated analysis branch that outputs fixed-size RGB raw frames suitable for direct RKNN pose input.
- `health-falld` consumes those RGB frames over the existing analysis socket and injects them into the pose model with minimal additional work.
- `health-falld` no longer performs the default NV12 preprocessing path for the production analysis pipeline.

This preserves process independence while moving the heavy image manipulation into the multimedia pipeline where it belongs.

## Architecture

### Current `main`

1. `health-videod` produces MJPEG/JPEG for preview and analysis transport.
2. `health-falld` decodes JPEG and converts it into the pose model input.
3. Startup classification latency is acceptable.

### Current candidate

1. `health-videod` taps NV12 frames from the preview pipeline.
2. `health-falld` performs NV12 preprocessing on CPU.
3. Startup classification latency regresses badly.

### New design

1. `health-videod` keeps the preview branch untouched:
   - preview remains MJPEG over TCP for the UI.
2. `health-videod` runs a dedicated analysis branch:
   - decode or camera source
   - colorspace conversion in GStreamer
   - scaling in GStreamer
   - fixed analysis FPS in GStreamer
   - output fixed-size RGB raw frames
3. `health-videod` publishes RGB analysis frames over the existing analysis transport.
4. `health-falld` consumes fixed-size RGB frames and uses a thin input path into RKNN.

## Service Responsibilities

### `health-videod`

Responsible for:

- sourcing camera/test-file frames
- preserving the UI preview path as-is
- preparing analysis frames in a pose-model-friendly RGB format
- rate-limiting analysis FPS to a stable value
- publishing analysis frames through the existing IPC channel

Not responsible for:

- fall classification
- RKNN pose inference
- UI preview rendering logic beyond current behavior

### `health-falld`

Responsible for:

- receiving RGB analysis frames
- thin validation of frame payload shape
- RKNN pose inference
- post-processing detections into tracked people
- LSTM/rule-based action classification

Not responsible for:

- default production-path NV12 preprocessing
- MJPEG/JPEG parsing for the new analysis path

## Data Contract

The existing `AnalysisFramePacket` remains the IPC unit, but the default production analysis payload changes to RGB raw.

Expected analysis payload for the new path:

- `pixel_format = rgb`
- `width = pose_input_width`
- `height = pose_input_height`
- `payload.size() == width * height * 3`

The protocol layer must reject malformed RGB packets.

## Detailed Design

### 1. Keep UI preview unchanged

The current preview TCP MJPEG branch remains the UI source of truth. No UI-facing API or transport semantics change in this design.

This avoids destabilizing preview latency or breaking existing dashboard behavior while work is focused on fall analysis.

### 2. Move image preprocessing into GStreamer

The analysis branch should be refactored so GStreamer performs the expensive per-frame operations that are currently happening inside `health-falld`.

Target operations in GStreamer:

- decode if source is test file
- colorspace conversion to RGB
- scaling to the pose model input dimensions
- stable analysis frame-rate limiting

Preferred first version:

- output RGB raw frames already matching the RKNN pose model input width and height
- remove the need for `health-falld` to perform per-pixel NV12 conversion in the default path

### 3. Thin RGB input path in `health-falld`

`health-falld` should add a fast RGB path in `rknn_pose_estimator.cpp`:

- validate RGB payload size and dimensions
- inject RGB payload directly into `rknn_inputs_set`
- avoid CPU image conversion for the production path

The old NV12 preprocessing code may remain in the repository for fallback/testing, but it should no longer be the default analysis path once the new pipeline is enabled.

### 4. Protocol support for RGB

`analysis_stream_protocol.cpp` currently validates known pixel formats and payload shapes. It must be extended or finalized to support RGB payloads explicitly and reject inconsistent frame shapes.

This keeps the cross-process boundary strict and debuggable.

## Performance Intent

This design does not claim zero-copy transport. It is instead a targeted correction of where preprocessing work happens.

Expected gain comes from:

- replacing slow CPU-side NV12 preprocessing in `health-falld`
- leveraging GStreamer pipeline elements for colorspace conversion and scaling
- keeping `health-falld` focused on inference and classification

Primary success target:

- candidate startup classification latency is no worse than `main`

Secondary target:

- reduce `health-falld` pose preprocess time from about 60 ms/frame toward the `main` baseline range

## Validation Plan

### Local validation

- protocol tests for RGB payload encode/decode and size validation
- pipeline-construction tests verifying the analysis branch outputs fixed-size RGB raw frames
- pose-estimator tests verifying RGB fast-path behavior does not require NV12 preprocessing

### Board validation

Use the same board and input file already used for regression testing:

- board: RK3588 development board
- input: `/home/elf/Videos/video.mp4`
- start mode: existing `start_test_input` path

Capture and compare:

- startup classification latency
- first useful state (`monitoring`, `stand`, etc.)
- pose-stage timing breakdown:
  - preprocess
  - `rknn_inputs_set`
  - `rknn_run`
  - `rknn_outputs_get`
  - post-process

Acceptance condition for this version:

- candidate startup classification latency is no worse than `main` on real hardware

## Risks

### Risk: fixed-size RGB transport increases payload size

RGB frames are larger than JPEG payloads. Even with that, this design is still expected to outperform the current NV12 path because it removes the dominant 60 ms/frame CPU preprocessing bottleneck.

### Risk: letterbox handling mismatch

If the pose model requires exact letterbox semantics that are not fully reproduced in GStreamer, accuracy could drift. The implementation must either:

- fully match the required preprocessing geometry in GStreamer, or
- leave only a tiny geometry/padding step in `health-falld`

### Risk: analysis FPS chosen poorly

If analysis FPS is too low, startup latency remains poor. If too high, downstream inference may overload. The implementation should begin with a stable analysis FPS chosen to match or beat `main` startup latency.

## Why This Version Stops Here

This design intentionally does not include shared buffers or DMABUF direct-consumption work. Those may still be worthwhile later, but they are not required to answer the current product need:

- restore analysis responsiveness
- preserve service isolation
- avoid destabilizing the preview/UI chain

## File-Level Impact Direction

Likely affected areas in the implementation plan:

- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`
- `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h`
- `rk_app/src/shared/protocol/analysis_stream_protocol.cpp`
- `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp`
- `rk_app/src/health_falld/pose/rknn_pose_estimator.h`
- targeted tests under `rk_app/src/tests/video_daemon_tests/`
- targeted tests under `rk_app/src/tests/fall_daemon_tests/`

## Decision

Approved approach for implementation:

- Use GStreamer-side preprocessing for the analysis branch.
- Keep UI preview unchanged.
- Maintain `health-videod` / `health-falld` process isolation.
- Treat RGB fixed-size analysis frames as the new default production path for pose inference in this version.

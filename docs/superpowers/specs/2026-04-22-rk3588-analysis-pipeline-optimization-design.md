## RK3588 Analysis Pipeline Optimization Design

Date: 2026-04-22
Status: Approved for planning

## 1. Goal

Optimize the RK3588 camera-to-fall-detection path so the fall-analysis service no longer depends on the UI preview MJPEG stream.

This design intentionally keeps the current UI preview transport unchanged in this phase. The optimization target is the analysis data path only.

The desired outcome is:

- preserve independent backend services
- preserve `health-videod` as the single owner of the camera device
- remove the unnecessary `raw -> JPEG -> decoded image -> model input` round-trip from the fall-analysis path
- improve latency, CPU efficiency, and runtime stability for fall detection

Out of scope for this version:

- redesigning the UI preview path
- replacing the current `health-ui` preview consumer
- DMABUF preview rendering in the UI
- camera-driver changes in the kernel
- multi-camera generalization

## 2. User-Approved Scope

The user explicitly approved the following direction during discussion:

- treat the current performance problem as an architecture problem in the user-space media pipeline, not as a kernel camera-driver problem
- document the existing design problem in detail
- document the optimization plan in detail
- keep backend components independent
- keep the current UI preview solution unchanged in this phase
- prioritize fixing the fall-analysis data path

## 3. Existing Context

The current video stack is already split into independent services:

- `health-videod`
  - the only process allowed to open `/dev/video11`
  - manages preview, recording, and snapshot
  - exposes video control/status IPC
- `health-ui`
  - uses the local video control/status IPC
  - consumes preview frames from a local MJPEG TCP stream
- `health-falld`
  - consumes analysis frames from `health-videod`
  - runs pose estimation and fall classification

Current default camera path:

- camera id: `front_cam`
- device path: `/dev/video11`
- preview profile: `640x480`, `NV12`, `30fps`
- record profile: `1280x720`, `NV12`, `30fps`
- snapshot profile: `1920x1080`, `NV12`

Important architectural fact:

- the camera device is correctly single-owned by `health-videod`
- the main design problem is how `health-videod` currently exports frames to the analysis service

## 4. Problem Statement

### 4.1 What the fall-analysis service actually needs

The fall-analysis service does not fundamentally need JPEG images.

Its real requirement is an inference-friendly frame input, such as:

- `NV12`
- `RGB`
- another uncompressed or near-uncompressed frame representation suitable for preprocessing

The current pose path in `health-falld` ultimately converts the incoming payload into a model input buffer. That means compressed JPEG is not the natural working format for inference.

### 4.2 What the current system actually does

The current preview pipeline in `health-videod` is effectively:

```text
v4l2src -> video/x-raw,format=NV12 -> jpegenc -> multipartmux -> tcpserversink
```

This means the frame already exists as uncompressed `video/x-raw, format=NV12` before `jpegenc` runs.

However, the current analysis output path does not branch off at that point. Instead, it reuses the preview output behavior:

1. `health-videod` encodes preview frames to JPEG
2. `health-videod` exposes them as local `tcp_mjpeg`
3. the analysis output backend reconnects to that preview URL
4. it parses the multipart MJPEG stream again
5. it extracts JPEG payload bytes
6. it repackages those JPEG bytes into `AnalysisFramePacket`
7. `health_falld` receives the packet
8. `health_falld` decodes JPEG into `QImage`
9. `health_falld` converts to `RGB888`
10. `health_falld` performs letterboxing and copies the data again into the RKNN input buffer

So the current analysis path is not:

```text
camera raw frame -> analysis preprocess -> inference
```

It is:

```text
camera raw frame -> JPEG preview encode -> MJPEG transport -> JPEG decode -> preprocess -> inference
```

### 4.3 Why this is a design problem

The existing architecture makes the analysis path depend on a preview-oriented transport format.

That is wrong for three reasons:

1. Preview and analysis have different data needs
   - preview needs a display-friendly transport
   - analysis needs an inference-friendly transport
2. The preview format was chosen for UI consumption convenience, not for model efficiency
3. Analysis becomes indirectly coupled to preview behavior, even though preview is not its real upstream dependency

This is therefore not a simple local inefficiency. It is a data-plane design error.

### 4.4 Observable consequences

The current design adds avoidable costs:

- JPEG encode in `health-videod`
- local TCP transport overhead
- multipart parsing overhead
- repeated `QByteArray` append/remove/slice activity
- JPEG decode in `health_falld`
- image format conversion in `health_falld`
- extra packing copy into the RKNN input buffer

The likely runtime effects are:

- increased fall-detection latency
- increased CPU usage
- reduced headroom during simultaneous preview + record + infer workloads
- more brittle coupling between video preview behavior and analytics behavior

## 5. Source-Level Evidence

The design problem is directly visible in the current source tree.

### 5.1 Preview pipeline encodes raw frames into JPEG

`rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`

- `buildPreviewCommand()` constructs:
  - `v4l2src`
  - `video/x-raw,format=NV12`
  - `jpegenc`
  - `multipartmux`
  - `tcpserversink`

This confirms the uncompressed frame exists before JPEG encoding.

### 5.2 Analysis output reconnects to the preview stream

`rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`

- `configurePreviewSource()` validates `transport=tcp_mjpeg`
- `previewSocket_` connects to the preview host/port
- `processPreviewChunk()` reparses multipart MJPEG
- `broadcastFrame()` repackages the extracted JPEG into `AnalysisFramePacket`

This confirms the analysis path is derived from preview output instead of directly from the capture path.

### 5.3 Fall analysis decodes JPEG back into an image buffer

`rk_app/src/health_falld/pose/rknn_pose_estimator.cpp`

- `QImage::fromData(frame.payload, "JPEG")`
- `convertToFormat(QImage::Format_RGB888)`
- letterboxing into a new image
- another packed buffer copy for RKNN input

This confirms the system pays the full JPEG decode and repack cost before inference.

## 6. Design Principles for the Fix

The optimization must satisfy the following non-negotiable principles:

1. Camera ownership remains inside `health-videod`
2. `health-ui` remains independent and unchanged in this phase
3. `health-falld` remains an independent service
4. The analysis path must no longer depend on preview transport
5. Recording must remain independent of analysis health
6. Failure in the analysis pipeline must not break preview or recording

## 7. Approaches Considered

### Recommended: introduce a dedicated analysis frame branch in `health-videod`

Summary:

- keep the current preview pipeline for UI unchanged
- add a separate analysis output path in `health-videod`
- analysis frames come from pre-JPEG raw video frames, not from the preview MJPEG stream

Pros:

- directly fixes the architectural error
- preserves component independence
- avoids destabilizing the UI preview in this phase
- gives immediate latency and CPU benefits on the fall-analysis path
- keeps future room for later transport upgrades

Cons:

- requires redesign of the analysis frame export path
- requires changes to both `health-videod` and `health-falld`

### Alternative: keep MJPEG preview and only optimize JPEG decode in `health_falld`

Pros:

- smallest code change

Cons:

- does not fix the root cause
- preview and analysis remain incorrectly coupled
- still pays the JPEG encode cost in the upstream path

### Alternative: full shared-buffer or DMABUF redesign now

Pros:

- highest long-term performance ceiling

Cons:

- too large for the current phase
- would unnecessarily mix the urgent analysis fix with a UI transport redesign
- increases delivery and stability risk for this iteration

## 8. Recommended Architecture

### 8.1 High-Level Design

Keep the current preview path unchanged and create a dedicated analysis branch:

```text
/dev/video11
   |
   v
health-videod
   |- preview path    -> existing tcp_mjpeg -> health-ui
   |- record path     -> existing h264/mp4 output
   `- analysis path   -> dedicated analysis frame output -> health-falld
```

The key change is that the analysis path branches from the capture side before JPEG preview encoding.

### 8.2 What changes conceptually

Old model:

- preview output is treated as the source for analysis

New model:

- preview output and analysis output are sibling consumers of the same captured frame source

That restores a correct separation of concerns:

- preview is for human display
- analysis is for inference

## 9. Analysis Data Path Design

### 9.1 Capture-side branch point

The dedicated analysis path must branch from the uncompressed frame path before `jpegenc`.

The most important rule is:

- analysis output must not be produced by reconnecting to `previewUrl`

Instead, `health-videod` must produce analysis frames directly from the capture pipeline.

### 9.2 Analysis transport format

For this phase, the analysis transport should carry uncompressed frame content, not JPEG.

Recommended starting point:

- `NV12`

Why `NV12`:

- it already matches the current capture-side caps in the existing video pipeline
- it avoids the JPEG encode/decode round-trip
- it keeps the path closer to the hardware-native video flow

The protocol must explicitly identify:

- width
- height
- pixel format
- payload size
- timestamp
- frame id

### 9.3 Analysis transport protocol

Retain the current idea of a dedicated high-frequency analysis stream between `health-videod` and `health-falld`, but change its payload semantics.

Suggested packet model:

```text
AnalysisFramePacket
- magic/version
- frame_id
- timestamp_ms
- camera_id
- width
- height
- pixel_format
- payload_size
- payload_bytes
```

For this design phase:

- `pixel_format` must no longer imply `JPEG` only
- `NV12` must become a supported first-class transport format

### 9.4 Delivery policy

Analysis is a real-time consumer, not an archival consumer.

Recommended policy:

- keep only the latest frame if the analysis service falls behind
- do not build unbounded frame queues
- prefer frame dropping over pipeline backpressure that could damage preview or recording

This must be explicit in the implementation strategy.

## 10. health-videod Changes

`health-videod` keeps camera ownership and adds a dedicated analysis export responsibility.

Required design changes:

- stop deriving analysis from `previewUrl`
- introduce a true analysis source inside the capture pipeline
- emit `AnalysisFramePacket` from that dedicated source
- preserve current preview and record behavior

`health-videod` must continue to treat analysis as optional:

- if no analysis client is connected, preview and recording still work
- if analysis export fails, preview and recording still work

## 11. health-falld Changes

`health_falld` must change its ingest expectation from `JPEG image payload` to `inference-oriented frame payload`.

Required design changes:

- `AnalysisStreamClient` continues to receive `AnalysisFramePacket`
- the estimator path must accept `NV12` input
- preprocessing must convert from the new packet format to the model input format

This still allows preprocessing copies where needed, but it removes the most wasteful encode/decode stage.

The intended new logical path is:

```text
NV12 frame -> preprocess/resize/convert -> RKNN input
```

instead of:

```text
JPEG bytes -> decode -> RGB image -> preprocess -> RKNN input
```

## 12. UI Design for This Phase

The UI preview path is intentionally unchanged in this version.

That means:

- `health-ui` continues to consume the current preview transport
- `VideoPreviewConsumer` remains as-is
- `VideoPreviewWidget` remains as-is
- no shared-buffer or DMABUF work is included in this document

This scope choice is intentional because the current priority is to fix the analytics-side design error without destabilizing an already working preview path.

## 13. Stability Strategy

The analysis path must be isolated from preview and recording.

Required stability rules:

- preview failure must not imply analysis failure
- analysis failure must not stop preview
- recording must not depend on analysis health
- analysis reconnect must be supported without restarting the camera
- no restart of the preview path is required merely because analysis reconnects

This isolation is one of the main product-quality benefits of the redesign.

## 14. Observability Requirements

This redesign should add explicit observability around the analysis path.

Recommended metrics or logs:

- frame capture timestamp
- frame export timestamp
- frame receive timestamp in `health_falld`
- inference start timestamp
- inference finish timestamp
- dropped-frame count
- current analysis input format
- analysis transport errors

These measurements are important to verify that the redesign actually reduces latency and CPU cost.

## 15. Validation Strategy

Validation must prove both correctness and architectural separation.

### Functional validation

- preview still works unchanged
- recording still works unchanged
- fall analysis still receives frames and produces results
- analysis reconnect works

### Performance validation

- compare end-to-end analysis latency before/after
- compare CPU usage during preview + infer
- compare CPU usage during preview + record + infer

### Architectural validation

- confirm analysis no longer connects to the preview URL path
- confirm `JPEG` is no longer the analysis transport format
- confirm preview and analysis can fail independently

## 16. Risks and Mitigations

### Risk: preprocessing complexity increases in `health_falld`

Mitigation:

- accept a preprocessing copy where needed in this phase
- the main goal is to remove JPEG encode/decode, not to force zero-copy immediately

### Risk: analysis path can still backpressure the capture graph

Mitigation:

- use bounded queues
- prefer latest-frame semantics
- make dropping explicit and observable

### Risk: scope creep into UI redesign

Mitigation:

- explicitly keep `health-ui` unchanged in this document
- treat preview transport redesign as a later, separate phase

## 17. Decision

Adopt the recommended architecture:

- preserve the existing UI preview path unchanged for now
- redesign the analysis export path in `health-videod`
- branch analysis from uncompressed frames before JPEG preview encoding
- transport `NV12` analysis frames to `health-falld`
- update `health_falld` preprocessing to consume that format directly

This is the smallest design change that fixes the root architectural mistake while preserving service independence and limiting product risk.

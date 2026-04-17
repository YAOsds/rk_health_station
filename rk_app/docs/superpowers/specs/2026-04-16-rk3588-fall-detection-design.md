# RK3588 Fall Detection Backend Design

## 1. Purpose and Scope

This document defines a backend-only integration design for deploying `yolov8n-pose` on RK3588 within the existing `rk_app` architecture, while keeping the current camera preview, snapshot, and recording chain intact.

The immediate goal is to add a live fall-detection backend path based on the existing camera in `health-videod`, without requiring UI integration in the first phase.

This design must satisfy the following constraints:

- Do not break or regress the existing `health-videod -> health-ui` preview chain.
- Do not deeply couple AI inference logic into the current video service.
- Keep camera ownership single-sourced.
- Allow staged rollout, rollback, and degraded operation.
- Reuse the validated deployment direction:
  - `YOLOv8-pose` on RKNN/NPU
  - `ST-GCN` on CPU in the first phase

Out of scope for this phase:

- UI visualization of keypoints or fall overlays
- Historical event pages or cloud sync
- Multi-person production-grade tracking
- Full replacement of existing video pipeline behavior

## 2. Existing Project Context

The current `rk_app` camera architecture is already split into clean service boundaries:

- `health-videod`
  - owns the camera device
  - manages preview, snapshot, and recording
  - exposes video control/status over local IPC
- `health-ui`
  - requests status and control via `QLocalSocket`
  - consumes preview frames from a local MJPEG TCP stream
- `shared`
  - contains data models and protocol serialization

Current video behavior:

- default camera id: `front_cam`
- default device: `/dev/video11`
- preview: `640x480@30`, `NV12`, delivered as local `tcp_mjpeg`
- snapshot: `1920x1080`
- record: `1280x720@30`, `H264/MP4`

The key architectural takeaway is that camera access and UI rendering are already separated. The new AI path should preserve that separation instead of bypassing it.

## 3. Design Principles

The integration follows five non-negotiable principles:

1. Camera ownership remains inside `health-videod`.
2. AI ownership remains inside a new independent service.
3. Video state and AI state are modeled separately.
4. Frame transport and business/result IPC are modeled separately.
5. AI failure may degrade analytics, but must not degrade the existing video chain.

These principles are specifically intended to prevent deep coupling and to keep the original engineering stable.

## 4. Recommended Architecture

### 4.1 High-Level Structure

Introduce a new daemon, `health-falld`, and extend `health-videod` with an analysis output capability.

```text
/dev/video11
   |
   v
health-videod
   |- preview pipeline  -> tcp mjpeg -> health-ui
   |- record pipeline   -> mp4 file
   |- snapshot pipeline -> jpeg file
   `- analysis output   -> local analysis stream -> health-falld

health-falld
   |- frame ingest
   |- preprocess
   |- yolov8-pose (RKNN/NPU)
   |- skeleton sequence buffer
   |- ST-GCN (CPU first phase)
   `- status/event IPC -> future UI / alert consumers
```

### 4.2 Why This Architecture

This design is preferred because:

- it preserves single ownership of `/dev/video11`
- it keeps AI logic out of `health-videod`
- it allows AI to crash or restart independently
- it matches the existing service-and-IPC style already used in `rk_app`
- it provides a clean path for later UI integration without revisiting camera access design

## 5. Responsibilities by Service

### 5.1 `health-videod`

`health-videod` keeps its current role:

- manage camera resource ownership
- manage preview/record/snapshot pipelines
- expose video status and control IPC

New responsibility added:

- provide an optional analysis frame output channel

`health-videod` must not:

- load RKNN models
- load ST-GCN models
- maintain fall-classification windows
- perform fall business-state decisions
- include AI-specific runtime types in core video classes

### 5.2 `health-falld`

`health-falld` is the dedicated AI service. It is responsible for:

- consuming analysis frames from `health-videod`
- decoding or converting input frames
- running `yolov8-pose.rknn`
- extracting 17-keypoint skeletons
- building the `45` frame temporal sequence
- running `ST-GCN`
- smoothing model output into business events
- publishing AI runtime status and fall events

### 5.3 `health-ui`

For the current phase, `health-ui` remains unchanged in behavior. It is intentionally excluded from the first implementation stage so backend integration can be validated independently.

### 5.4 `shared`

`shared` only carries:

- pure data models
- protocol encode/decode helpers
- no RKNN, OpenCV, ORT, or GStreamer runtime objects

## 6. Data Flow Design

### 6.1 Core Flow

The original video chain remains untouched:

```text
camera -> health-videod -> preview / record / snapshot
```

The new AI chain is a side path:

```text
camera -> health-videod -> analysis output -> health-falld -> result / event
```

This side path is intentionally independent of the UI preview transport.

### 6.2 Why Not Reuse Existing Preview MJPEG

The existing preview stream is a UI display transport, not an analytics transport. Reusing it would create hidden coupling between UI-facing preview behavior and AI-facing inference requirements.

Specific reasons to avoid reuse:

- preview format may change for UI reasons
- preview prioritizes viewability, not inference stability
- AI requires a controlled profile for fps and frame size
- preview protocol should stay free to evolve independently

### 6.3 Analysis Input Format Strategy

Two-stage strategy:

- Phase 1: `JPEG` packet transport
  - fastest to land
  - easiest to align with current GStreamer export style
- Phase 2: `NV12` packet transport
  - lower long-term overhead
  - more suitable for production optimization

The protocol must include a `pixel_format` field from day one so the transport can evolve without redesigning the interface.

## 7. Interface and Protocol Design

### 7.1 Protocol Split

Two distinct protocols are required.

#### A. Analysis Stream Protocol

Used between `health-videod` and `health-falld`.

- purpose: high-frequency frame transport
- transport: Unix domain socket
- encoding: binary packet
- reason: frame payload is too heavy for JSON line IPC

Suggested packet shape:

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

#### B. Fall IPC Protocol

Used by `health-falld` to expose status/results to future consumers.

- purpose: control, status, result query, event distribution
- transport: `QLocalSocket`
- encoding: JSON line protocol
- reason: consistent with the existing project style

### 7.2 Status Models

Do not extend `VideoChannelStatus` with AI runtime fields. Add separate models instead.

Suggested models:

```text
AnalysisChannelStatus
- camera_id
- enabled
- stream_connected
- output_format
- width
- height
- fps
- dropped_frames
- last_error

FallRuntimeStatus
- camera_id
- input_connected
- pose_model_ready
- action_model_ready
- current_fps
- last_frame_ts
- last_infer_ts
- latest_state
- latest_confidence
- last_error
```

### 7.3 Result Models

Suggested intermediate result:

```text
PoseFrameResult
- frame_id
- timestamp_ms
- camera_id
- person_count
- persons[]
  - bbox
  - score
  - keypoints[17][3]
```

Suggested business result:

```text
FallDetectionResult
- frame_id
- timestamp_ms
- camera_id
- state
- confidence
- event_flag
```

Suggested business event:

```text
FallEvent
- event_id
- camera_id
- ts_start
- ts_confirm
- event_type
- confidence
- snapshot_ref
- clip_ref
```

These models deliberately hide RKNN tensors and algorithm-internal runtime details.

## 8. Module and Directory Design

### 8.1 Video Domain Additions

Keep the current video domain intact and add only analysis-output related modules.

Suggested additions:

```text
src/health_videod/analysis/
  analysis_output_backend.h
  gstreamer_analysis_output_backend.h/.cpp
  analysis_stream_packet.h/.cpp
```

Responsibilities:

- `analysis_output_backend`
  - abstract how analysis frames are emitted
- `gstreamer_analysis_output_backend`
  - produces analysis frames from the video side
- `analysis_stream_packet`
  - packet encode/decode helpers

### 8.2 New AI Service

Add a new service tree:

```text
src/health_falld/
  app/
  ingest/
  pose/
  action/
  domain/
  ipc/
  runtime/
```

Suggested breakdown:

```text
src/health_falld/app/
  fall_daemon_app.h/.cpp
  main.cpp

src/health_falld/ingest/
  analysis_stream_client.h/.cpp
  frame_decoder.h/.cpp

src/health_falld/pose/
  pose_estimator.h
  rknn_pose_estimator.h/.cpp
  pose_postprocess.h/.cpp
  pose_types.h

src/health_falld/action/
  action_classifier.h
  stgcn_action_classifier.h/.cpp
  sequence_buffer.h/.cpp
  target_selector.h/.cpp

src/health_falld/domain/
  fall_detector_service.h/.cpp
  fall_event_policy.h/.cpp
  fall_result_types.h

src/health_falld/ipc/
  fall_gateway.h/.cpp
  fall_protocol.h/.cpp

src/health_falld/runtime/
  model_paths.h/.cpp
  inference_worker.h/.cpp
  runtime_config.h/.cpp
```

### 8.3 Shared Additions

Suggested shared additions:

```text
src/shared/models/fall_models.h
src/shared/protocol/fall_ipc.h/.cpp
src/shared/protocol/analysis_stream_protocol.h/.cpp
```

Shared must contain only protocol-safe data and serialization helpers.

### 8.4 Dependency Direction

The dependency graph must remain one-way:

```text
shared <- health_videod
shared <- health_falld
shared <- health_ui

health_videod X-> health_falld
health_videod X-> rknn / stgcn internals
health_ui X-> AI runtime internals
```

This is the main code-level guardrail against deep coupling.

## 9. Runtime Design

### 9.1 Startup and Steady-State

`health-videod`:

- starts exactly as before
- launches preview as before
- optionally launches analysis output if enabled by config

`health-falld`:

- loads config
- connects to analysis stream
- loads pose runtime
- loads action runtime
- enters the live inference loop

Per-frame processing:

```text
ingest frame
 -> decode/convert
 -> pose inference
 -> extract 17 keypoints
 -> update sequence buffer
 -> if enough frames, run ST-GCN
 -> smooth state
 -> publish result/event
```

### 9.2 Thread Model

Recommended logical split:

- ingest thread
  - receive packets
  - push bounded frames to queue
- inference thread
  - decode/convert
  - pose inference
  - sequence update
  - ST-GCN inference
- publish/control thread
  - expose status
  - publish events
  - maintain health information

This split keeps frame input, inference, and outward-facing control isolated from each other.

### 9.3 Queue Strategy

This system is real-time oriented, not lossless oriented.

Rules:

- use a bounded queue
- keep latest frames
- drop old frames under load
- never allow unbounded accumulation
- never let AI backlog turn into camera-side backpressure

The core policy is:

- dropping frames is acceptable
- blocking the camera chain is not acceptable

## 10. Performance and Resource Strategy

### 10.1 Analysis Profile

Recommended first-phase analysis profile:

- resolution: `640x640`
- fps: `8-10`
- one primary person
- sequence length: `45`

This matches the current fall-classification pipeline assumptions without introducing unnecessary complexity.

### 10.2 Model Placement

Recommended first-phase runtime split:

- `yolov8-pose`: RKNN/NPU
- `ST-GCN`: CPU

This is the lowest-risk deployment path because pose is the heavy stage and has already been validated on board, while the ST-GCN path is lightweight enough to keep on CPU initially.

### 10.3 Primary Target Selection

The first version should classify a single primary person only.

Suggested target selection priority:

1. highest confidence
2. largest area
3. temporal continuity relative to the previous frame

This aligns with the current single-subject temporal classifier assumptions and keeps the first implementation controlled.

## 11. Business-State Policy

Model output must not be treated as a direct final alarm.

Recommended separation:

```text
Raw action classes:
- stand
- fall
- lie

Business states:
- monitoring
- suspected_fall
- fall_confirmed
- recovered
```

Recommended policy:

- require repeated `fall` or `lie` evidence before escalation
- require sustained evidence before confirming an event
- require repeated `stand` evidence before recovery
- apply cooldown to avoid repeated alerts for the same sustained incident

This policy layer belongs in the domain layer, not in the model wrapper.

## 12. Fault Isolation and Recovery

### 12.1 `health-videod` Rules

- analysis output failure must not stop preview
- analysis output failure must not stop recording
- analysis output state must not overwrite camera state semantics
- analysis output must be independently enabled and disabled

### 12.2 `health-falld` Rules

- reconnect automatically when analysis input disconnects
- tolerate single-frame inference failures
- allow degraded states when one model is unavailable
- keep runtime health status observable

Suggested degraded examples:

- pose unavailable -> no analytics
- pose available but action unavailable -> expose pose-ready and action-degraded state

## 13. Configuration and Assets

Configuration and model assets must be externalized rather than hardcoded.

Suggested assets:

```text
assets/models/
  yolov8n-pose.rknn
  stgcn_fall.onnx

config/
  fall_detection.yaml
```

Suggested configurable fields:

- analysis enable flag
- analysis socket name
- analysis resolution
- analysis fps
- pose thresholds
- sequence length
- fall confidence threshold
- debounce parameters
- cooldown parameters
- model paths

### 13.1 Default Safety Policy

Analysis must be disabled by default in the first integration.

Example intent:

```text
video.analysis.enabled=false
```

This ensures that merging the new capability does not change existing default behavior until explicitly enabled.

## 14. Testing Strategy

Testing must prove two things:

1. the original chain still behaves the same
2. the new chain behaves correctly in isolation

### 14.1 Original Chain Regression Tests

Existing tests for:

- `video_service`
- `video_gateway`
- `video_preview_consumer`
- `video_monitor_page`

must remain valid without requiring the AI path to be enabled.

### 14.2 Protocol and Logic Unit Tests

Add focused tests for:

- analysis stream packet encode/decode
- sequence buffer behavior
- target selection
- event policy / debounce rules
- fall protocol JSON encode/decode

### 14.3 Service Integration Tests

Add tests that simulate:

- fake analysis producer -> `health-falld`
- fake consumer <- `health-falld`
- reconnect and degraded-mode behavior

### 14.4 Board-Level Validation

On RK3588, validate:

- preview remains healthy
- recording remains healthy
- `health-falld` runs concurrently
- pose inference runs on NPU
- action inference runs on CPU
- long-running execution does not accumulate unbounded delay

## 15. Implementation Phases

To minimize risk, implementation should be staged.

### Phase 0: Baseline Protection

- confirm existing video behavior and tests
- lock down default configuration assumptions

### Phase 1: Analysis Output Only

- add analysis output support to `health-videod`
- validate with a fake consumer
- prove no regression to preview/record/snapshot

### Phase 2: `health-falld` Skeleton Service

- build the daemon shell
- connect to analysis stream
- expose runtime status
- no real models yet

### Phase 3: Pose Integration

- integrate `yolov8-pose.rknn`
- output `PoseFrameResult`

### Phase 4: Action and Event Integration

- integrate ST-GCN
- integrate state smoothing and event policy
- output `FallDetectionResult` and `FallEvent`

### Phase 5: Consumer Integration

- UI
- alerts
- history

This phase is intentionally outside the immediate task.

## 16. Mapping to Current Assets

This design builds on current validated work:

- `rknn_model_zoo/examples/yolov8_pose`
  - deployment and postprocess reference for pose on RKNN
- `yolo_detect`
  - temporal skeleton organization and ST-GCN reference logic
- `rk_app`
  - service layout, IPC conventions, and camera ownership model

The important architectural rule is that these existing assets are inputs to the design, not directories to be copied wholesale into the product runtime.

## 17. Final Decision Summary

The chosen architecture is:

- keep `health-videod` as the sole camera owner
- add an optional analysis output channel to `health-videod`
- create an independent `health-falld` daemon for pose and fall inference
- keep frame transport separate from business-result IPC
- keep AI state separate from video state
- keep analytics disabled by default until explicitly enabled

This design preserves the original chain, minimizes coupling, supports staged rollout, and provides a clean foundation for later UI integration.

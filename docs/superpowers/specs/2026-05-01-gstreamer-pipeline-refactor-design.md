# GStreamer Pipeline Refactor Design

Date: 2026-05-01
Status: Approved for planning

## 1. Goal

Refactor `health-videod`'s GStreamer pipeline code so preview lifecycle, command construction, analysis-frame publishing, and low-level transport helpers no longer live in the same large class.

The design goal is maintainability and safer iteration, not behavior change. Preview, recording, snapshot, analysis transport, and runtime configuration semantics must remain compatible with the current implementation.

## 2. User-Approved Direction

The approved direction is:

- accept the current class structure as the main design problem
- split responsibilities by change reason, not just by file length
- improve testability by removing the need for `friend` access and private-state setup in tests
- apply the same refactoring principle to `InprocessGstreamerPipeline`
- avoid over-design; only introduce factory indirection where there is real runtime strategy selection

## 3. Current Problem

`GstreamerVideoPipelineBackend` currently mixes four independent concerns:

1. command construction
2. process lifecycle and stop/start policy
3. analysis-frame conversion and publishing
4. low-level IO helpers such as MJPEG multipart parsing, preview TCP frame reads, and DMA-BUF allocation

These concerns change for different reasons:

- command strings change when pipeline topology changes
- lifecycle logic changes when process supervision rules change
- frame publishing changes when analysis transport or conversion rules change
- IO helpers change when DMA-BUF or preview transport details change

Because they are coupled in one type, a change in one area requires re-reading unrelated code paths before it is safe to modify anything.

## 4. Evidence in the Current Code

The current structure already shows boundary leakage:

- the backend header exposes command builders, transport parsing helpers, lifecycle methods, and frame-processing methods in one class
- `ActivePipeline` stores process handles, in-process pipeline handles, analysis-format metadata, frame counters, stdout buffers, shared-memory writers, and runtime log stats in one struct
- tests depend on `friend class GstreamerVideoPipelineBackendTest` and directly mutate private backend state to exercise DMA analysis paths

This is not only a readability issue. It is a boundary-design issue that makes testing and future changes more fragile.

## 5. Design Principles

The refactor must follow these rules:

1. Preserve current external behavior.
2. Keep the default external `gst-launch` path and the optional in-process GStreamer path working.
3. Separate orchestration from implementation details.
4. Prefer composition over inheritance.
5. Introduce interfaces only at boundaries that truly need interchangeable implementations.
6. Keep internal helper types concrete unless there is a demonstrated need for runtime selection or mocking.

## 6. Recommended Architecture

### 6.1 Backend Role

`GstreamerVideoPipelineBackend` becomes a coordinator. Its job is limited to:

- receiving public backend API calls
- selecting preview execution mode
- creating or acquiring a per-camera session object
- wiring callbacks between lifecycle and analysis publishing
- forwarding runtime errors to `VideoPipelineObserver`

It should no longer contain command-string assembly, direct frame conversion logic, or transport helper implementations.

### 6.2 Per-Camera Session

Promote the current `ActivePipeline` state into a focused session object, for example `PipelineSession`.

Its responsibility is to hold mutable per-camera state:

- camera id and preview URL
- preview/recording running state
- process or in-process runner handles
- analysis input/output geometry and format metadata
- next frame id
- shared-memory ring writer
- rolling log statistics

This keeps state ownership explicit and avoids spreading per-camera fields across multiple unrelated helpers.

### 6.3 Command Construction

Extract preview, recording, snapshot, and preview-stream-recording command generation into a concrete `GstCommandBuilder`.

Responsibilities:

- quote shell arguments
- map runtime config to launch binary selection
- build preview/recording/snapshot command strings
- build the external analysis tap fragment
- derive preview URL, port, and boundary helpers if they remain command-related

Non-responsibilities:

- starting processes
- reading sockets
- converting frames
- publishing descriptors

This class is a good extraction target because its behavior is already heavily covered by command-string tests.

### 6.4 External Process Lifecycle

Extract process supervision into `GstProcessRunner`.

Responsibilities:

- start `QProcess`
- perform startup probe timing
- connect stdout/stderr handling callbacks
- stop preview and recording processes with current SIGINT/kill fallback semantics
- surface exit failures through callbacks

This class owns lifecycle policy, not analysis logic.

### 6.5 Analysis Frame Publishing

Extract frame conversion and publication into `AnalysisFramePublisher`.

Responsibilities:

- accept raw byte frames or DMA input frames
- perform CPU or RGA conversion
- choose shared-memory or DMA-BUF descriptor publication
- update frame ids and timestamps
- write runtime log summaries and latency markers

This class should receive a session-like state object plus dependencies such as:

- `AnalysisFrameSource`
- `AnalysisFrameConverter`
- runtime analysis config

The important design point is that backend orchestration should not know how RGB payloads or DMA descriptors are produced.

### 6.6 IO and Transport Helpers

Low-level helpers should move out of the backend into focused utility classes or file-local helpers with narrow headers:

- `PreviewStreamReader`
  - parse preview URL
  - read one JPEG frame from TCP multipart MJPEG
- `MultipartJpegParser`
  - incremental multipart boundary parsing
- `DmaBufferAllocator`
  - allocate memfd or dma-heap buffers
  - mmap and write payloads when needed

These helpers do not need factories or broad interfaces in the first refactor step.

## 7. Factory and Interface Strategy

### 7.1 Where Factory Indirection Is Valuable

Factory indirection is justified at the pipeline execution boundary, because there is real runtime strategy selection today:

- external `gst-launch` preview path
- in-process GStreamer preview path

For that boundary, a small factory is useful. Example shape:

- `PipelineRunnerFactory`
- returns either `ExternalGstreamerRunner` or `InprocessGstreamerRunner`

The backend can then depend on a runner interface instead of directly constructing one concrete implementation path.

### 7.2 Where Factory Indirection Is Not the First Priority

Factory indirection is not the main value for:

- command builders
- preview stream readers
- DMA allocators
- multipart parsers

Those areas mainly need extraction and local clarity. Introducing interfaces there immediately would add ceremony without solving a concrete runtime-selection problem.

### 7.3 Recommendation

Do not make "introduce factories everywhere" the first step. The recommended order is:

1. split responsibilities into concrete components
2. stabilize data flow and tests around the new boundaries
3. add a factory only at the runner-selection boundary

This keeps the design simpler while still decoupling the only place that already has multiple execution strategies.

## 8. In-Process Pipeline Refactor

`InprocessGstreamerPipeline` should follow the same rule: keep it as a small lifecycle owner and move its major concerns into collaborators.

Recommended internal breakdown:

- `InprocessLaunchDescriptionBuilder`
  - builds the `gst_parse_launch()` string
- `GstBusMonitor`
  - polls the bus and maps EOS and error messages into runtime error callbacks
- `GstAppSinkFrameDispatcher`
  - handles `GstSample` extraction
  - chooses DMA input path or mapped byte fallback
  - emits queued callbacks onto the Qt thread

After this split, `InprocessGstreamerPipeline` should mainly own:

- `start()`
- `stop()`
- callback registration
- construction and teardown of GStreamer objects

This is enough abstraction for the current codebase. A larger plugin-style architecture is unnecessary at this stage.

## 9. Testing Strategy

The refactor should improve tests, not just move code around.

Target outcomes:

- command tests move to `GstCommandBuilder` without needing backend friendship
- analysis publication tests exercise `AnalysisFramePublisher` directly with fake converters and fake frame sources
- preview stream parsing tests target `PreviewStreamReader` and `MultipartJpegParser` directly
- backend tests focus on orchestration behavior:
  - backend selection
  - preview/recording lifecycle wiring
  - observer notifications
  - session cleanup

The direct mutation of backend private state in tests should disappear once `PipelineSession` and `AnalysisFramePublisher` have explicit seams.

## 10. Implementation Phases

### Phase 1: Safe Extraction

- extract `GstCommandBuilder`
- extract `PreviewStreamReader` and multipart parser helper
- extract DMA allocation helper
- keep backend behavior unchanged

### Phase 2: Session and Publisher Split

- introduce `PipelineSession`
- extract `AnalysisFramePublisher`
- move frame-conversion and descriptor publication logic out of backend

### Phase 3: Lifecycle Split

- extract `GstProcessRunner`
- move start/stop/supervision logic out of backend

### Phase 4: Runner Selection Abstraction

- add a narrow runner interface and factory for external vs in-process preview execution
- keep command builder and IO helpers concrete

### Phase 5: In-Process Internal Cleanup

- split `InprocessGstreamerPipeline` into launch-description builder, bus monitor, and appsink dispatcher collaborators

## 11. Risks and Controls

Main risks:

- subtle behavior changes in process shutdown and startup probe timing
- breaking analysis descriptor semantics during publisher extraction
- moving too many pieces at once and losing confidence in equivalence

Controls:

- refactor in phases with tests moved alongside each extraction
- preserve current public API and runtime config names
- avoid mixing behavioral changes with structural changes in the same patch
- keep the external `gst-launch` path as the baseline reference during refactor

## 12. Out of Scope

This design does not include:

- changing preview transport away from local MJPEG TCP
- redesigning `health-falld` analysis protocols
- changing camera ownership rules
- introducing a generic media graph framework
- replacing all concrete helpers with abstract interfaces

## 13. Final Recommendation

The proposed refactor is worth doing, but it should be executed in a narrower and more disciplined form than "add factories everywhere."

The highest-value sequence is:

1. extract command building
2. extract analysis frame publishing
3. introduce an explicit per-camera session object
4. then add a factory only for external vs in-process runner selection

That sequence addresses the real pain in the current codebase while avoiding unnecessary abstraction overhead.

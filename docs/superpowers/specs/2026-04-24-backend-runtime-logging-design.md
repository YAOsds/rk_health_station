# Backend Runtime Logging Design

## Goal

Add sparse, actionable runtime and performance logs that help diagnose RK3588 video-analysis behavior without causing log spam. Logging should focus on backend processes (`health-videod`, `health-falld`). UI logging should remain limited to errors and connection changes.

## Scope

### In scope
- Add low-frequency aggregated performance summaries in `health-videod`
- Add low-frequency aggregated performance summaries in `health-falld`
- Keep immediate logging for important state transitions and failures
- Reduce or avoid per-frame steady-state logs that spam `tail -f`
- Keep UI logging restricted to errors and connection changes

### Out of scope
- Full metrics export system
- Prometheus or external telemetry backends
- Per-frame debug tracing by default
- New runtime configuration surface unless absolutely necessary

## Approach Options

### Option 1: event-only logging
Only log start/stop/error/state-change events.
- Pros: quietest logs
- Cons: weak performance visibility; hard to spot FPS drops or inference slowdown

### Option 2: event logs plus periodic summaries (recommended)
Log critical events immediately and emit compact summaries every fixed interval.
- Pros: good observability without spam; easy to inspect with `tail -f`
- Cons: slightly more state bookkeeping

### Option 3: verbose logs behind env flag
Keep default quiet and add a verbose mode for detailed traces.
- Pros: flexible for deep debugging
- Cons: extra complexity and still less useful in default mode for ongoing monitoring

## Recommended Design

Use immediate event logs for important state transitions and a 5-second periodic summary in backend services.

## health-videod logging

### Immediate logs
Log once per meaningful event:
- preview started/stopped
- recording started/stopped
- test input started/stopped
- test playback finished
- pipeline runtime error
- analysis ring init failure
- analysis output backend start/stop failure

### Periodic summary
Emit at most once every 5 seconds per active camera while frames are flowing:
- `camera_id`
- `input_mode`
- `analysis_frames_published`
- windowed publish FPS
- total ring dropped frames and window delta
- whether analysis consumers are connected
- current preview state

Format should be one compact line, e.g.:
`video_perf camera=front_cam mode=test_file state=previewing fps=12.4 published=62 dropped_total=0 dropped_delta=0 consumers=1`

## health-falld logging

### Immediate logs
Keep or add immediate logs for:
- ingest connected/disconnected
- pose model load success/failure
- action model load success/failure
- first classification after stream start
- `fall_event`
- newly observed runtime error or error cleared

### Reduce noisy logs
Current steady-state `classification` / `classification_batch` logs are too chatty.
Adjust behavior so that:
- `fall` and `lie` remain immediate
- stable `stand` / `monitoring` are not logged every frame
- periodic summary covers the steady-state operational picture

### Periodic summary
Emit at most once every 5 seconds while frames are flowing:
- input frame count in window
- ingest FPS
- infer FPS
- average infer latency in ms for the window
- frames with detected people
- frames without detected people
- non-empty classification batch count
- latest state/confidence
- latest error if present

Format should be one compact line, e.g.:
`fall_perf camera=front_cam ingest_fps=12.1 infer_fps=12.1 avg_infer_ms=31.8 people_frames=58 empty_frames=4 nonempty_batches=41 state=stand conf=0.99 error=`

## UI logging

Keep only:
- backend connect/disconnect
- preview socket errors
- fall backend connection changes

Do not add high-frequency UI-side classification logs.

## Implementation Plan Shape

### Shared pattern
Implement lightweight in-process aggregators using counters and `QElapsedTimer` or timestamps.
No extra threads or locks are needed because the relevant callbacks already run on the owning Qt thread.

### `health-videod`
- add a small stats struct in the pipeline backend or service layer
- update counters when analysis descriptors are published
- flush a summary only when 5 seconds have elapsed and there has been activity
- reset only window counters, not lifetime counters

### `health-falld`
- add a small stats struct in `FallDaemonApp`
- update counters per received frame and per completed inference
- accumulate inference duration as `lastInferTs - frame-processing-start`
- count people/no-people frames and non-empty batch publications
- flush summary every 5 seconds when active

### log suppression behavior
- deduplicate repeated runtime errors by only logging when the error value changes
- only log error-clear transitions once
- only emit periodic summary when the window had activity

## Testing Strategy

### Automated
- add focused unit tests for summary throttling behavior where practical
- verify repeated high-frequency updates do not emit every call
- verify summary emits after interval elapses
- verify stale-state logs (e.g. repeated `stand`) are suppressed if implemented through helper logic

### Manual
Board verification in test mode using `/home/elf/Videos/video.mp4`:
- confirm `health-videod.log` shows start/stop events and low-frequency `video_perf`
- confirm `health-falld.log` shows low-frequency `fall_perf`
- confirm logs do not print one `stand` line per frame anymore
- confirm `fall` events still appear immediately when present

## Risks
- too little logging may hide rare transitions
- too much aggregation may hide short spikes
- timestamps based on wall clock should only be used for summaries, not correctness

## Mitigations
- keep critical transitions immediate
- keep summary interval short enough (5s) for interactive debugging
- preserve existing latency marker files for deeper measurement workflows

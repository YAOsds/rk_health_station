# RGA + RKNN Stage 1/2 Optimization Design

## Goal

Use the current `main` runtime as the performance baseline, then implement Scheme B: keep the existing process topology, have `health-videod` publish model-ready RGA-preprocessed RGB frames with letterbox metadata, and have `health-falld` use RKNN preallocated output buffers by default, with an opt-in full RKNN IO-memory path for board experiments.

## Baseline

Captured on RK3588 from `/home/elf/rk3588_bundle` before implementation:

- Commit/binaries: current `main` bundle, ARM aarch64 binaries.
- `start_all.sh` status: `healthd`, `health-videod`, `health-falld`, and `health-ui` all running.
- `video_perf`: stable at about `15.0 fps`, no dropped frames, one consumer.
- `fall_perf`: stable at about `15.0 ingest_fps` / `15.0 infer_fps`, `avg_infer_ms` around `24.2 ms`.
- Process snapshot after ~45s: `health-videod` about `2.5% CPU`, `health-falld` about `7.2% CPU`.
- Pose timing with `RK_FALL_POSE_TIMING_PATH=/tmp/rk_pose_timing_baseline.jsonl`, last 200 samples:
  - `preprocess_ms avg 0.0`
  - `inputs_set_ms avg 0.0`
  - `rknn_run_ms avg 21.61`
  - `outputs_get_ms avg 1.0`
  - `total_ms avg 23.77`

This baseline shows the current RGA RGB fast path already removes most CPU preprocessing cost. The realistic measurable win for Scheme B is primarily reducing output retrieval/copy overhead and preserving correct letterbox metadata for the model-ready frames.

## Scope

In scope:

- Add pose-preprocess metadata (`xPad`, `yPad`, `scale`, `preprocessedForPose`) to analysis packets/descriptors and shared-memory slots.
- Update the RGA converter so NV12 -> RGB can letterbox into the model input canvas instead of plain stretching.
- Let `health-falld` consume model-ready RGB packets directly with the metadata supplied by `health-videod`.
- Add RKNN output-buffer optimization in `RknnPoseEstimator`: preallocate output buffers during model load and pass them to `rknn_outputs_get` to avoid per-frame output allocation. Keep an opt-in `RK_FALL_RKNN_IO_MEM=zero_copy` path for full `rknn_set_io_mem` experiments.
- Keep fallback to the existing `rknn_inputs_set` / allocator-owned `rknn_outputs_get` path.
- Validate on RK3588 with the same bundle workflow and compare against the baseline metrics above.

Out of scope for this stage:

- Passing DMABUF fds between `health-videod` and `health-falld`.
- Replacing shared memory ring payloads with fd-backed frames.
- V4L2/GStreamer DMABuf caps negotiation and cache sync handling.

## Architecture

`health-videod` remains the owner of camera/GStreamer capture. Its analysis tap still emits NV12 frames from `gst-launch` stdout. The RGA converter receives the NV12 frame, fills a 640x640 RGB destination canvas with 114, resizes the source into a letterboxed ROI, and returns both the RGB payload and the letterbox metadata. The shared memory ring stores this metadata alongside the payload.

`health-falld` still receives descriptors over the local analysis stream and reads payload bytes from shared memory. If a packet is RGB, model-sized, and marked as pose-preprocessed, `RknnPoseEstimator` bypasses CPU letterbox and uses the provided metadata for post-processing coordinate restoration. If metadata is absent or invalid, it falls back to existing RGB/JPEG/NV12 preprocessing behavior.

For RKNN, `RknnPoseEstimator` initializes reusable output buffers when the real RKNN backend is enabled. On inference, it keeps the stable `rknn_inputs_set` / `rknn_run` path and asks `rknn_outputs_get` to write into those preallocated buffers. A full `rknn_set_io_mem` input/output path remains available with `RK_FALL_RKNN_IO_MEM=zero_copy`, but it is not the default because board measurements showed no better end-to-end timing. The default path targets the per-frame output allocation overhead without changing tensor layout semantics.

## Success Criteria

- Host tests pass and cover metadata serialization, shared-memory metadata preservation, RGA converter metadata propagation through the pipeline, and pose fast-path metadata consumption.
- RK3588 cross-build succeeds and produces ARM aarch64 binaries.
- Board `start_all.sh` launches all four services.
- Board runtime status still reports `input_connected=true`, `pose_model_ready=true`, `action_model_ready=true`, and `latest_state=monitoring`.
- Compared with baseline, pose timing should show `output_prealloc_path=true` and total pose `total_ms` should improve or at least not regress. The opt-in full IO-memory probe should show `io_mem_path=true` and `outputs_get_ms=0`, but it is only mergeable as default if it improves total timing.

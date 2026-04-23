# RK3588 Analysis Pipeline GStreamer RGB Validation Results

Date: 2026-04-23

## Scope

This validation checks the approved analysis-pipeline change:

- UI preview path stays on the existing MJPEG branch.
- `health-videod` emits fixed-size RGB analysis frames for `health-falld`.
- `health-falld` uses the RGB fast path and avoids the old NV12 CPU preprocessing in the production path.

## Host Verification

Focused host tests passed in `out/build-rk_app-host-tests`:

- `analysis_stream_protocol_test`
- `analysis_output_backend_test`
- `video_service_analysis_test`
- `gstreamer_video_pipeline_backend_test`
- `analysis_stream_client_test`
- `fall_runtime_pose_stub_test`
- `nv12_preprocessor_test`
- `rgb_pose_fast_path_test`
- `pose_stage_timing_logger_test`

Command used:

```bash
ctest --test-dir out/build-rk_app-host-tests -R \
  'analysis_stream_protocol_test|analysis_output_backend_test|video_service_analysis_test|gstreamer_video_pipeline_backend_test|analysis_stream_client_test|fall_runtime_pose_stub_test|nv12_preprocessor_test|rgb_pose_fast_path_test|pose_stage_timing_logger_test' \
  --output-on-failure
```

## RK3588 Build + Deploy

Bundle build:

```bash
BUILD_DIR=/tmp/rk_health_station-build-rk3588-featureopt \
BUNDLE_DIR=out/rk3588_bundle_latency_candidate \
bash deploy/scripts/build_rk3588_bundle.sh
```

Board deploy:

```bash
rsync -az --inplace --delete out/rk3588_bundle_latency_candidate/ \
  elf@192.168.137.179:/home/elf/rk3588_bundle_candidate/
```

## Board Validation Notes

The board root filesystem was full during validation, so the normal `start.sh` path could fail when creating pid/log files under `/home/elf/rk3588_bundle_candidate`.

To avoid that environmental false negative, the final A/B measurement used the same deployed binaries but started the services with sockets, sqlite DB, latency markers, and pose-stage traces placed under `/dev/shm`.

That workaround does not change the binaries under test; it only avoids filesystem exhaustion on the target.

Follow-up verification after disk space recovery:

- the normal bundle startup path `./scripts/start.sh --backend-only` was re-validated successfully
- `healthd`, `health-videod`, and `health-falld` all started normally
- the standard latency harness also ran successfully against the normal bundle startup flow

## A/B Latency Result

Test input:

- file: `/home/elf/Videos/video.mp4`
- success criterion for this milestone: candidate startup classification latency must be no worse than `main`

### Current `main` bundle

- bundle: `/home/elf/rk3588_bundle_main`
- result:
  - `startup_classification_latency_ms`: `2160`
  - `classification_stage_latency_ms`: `2153`
  - `analysis_ingress_latency_ms`: `7`
  - `first_state`: `stand`

### Candidate RGB pipeline bundle

- bundle: `/home/elf/rk3588_bundle_candidate`
- result:
  - `startup_classification_latency_ms`: `1315`
  - `classification_stage_latency_ms`: `1399`
  - `analysis_ingress_latency_ms`: `-84`
  - `first_state`: `stand`

### Candidate RGB pipeline bundle (normal `start.sh` flow after freeing disk space)

- bundle: `/home/elf/rk3588_bundle_candidate`
- result:
  - `startup_classification_latency_ms`: `1259`
  - `classification_stage_latency_ms`: `1362`
  - `analysis_ingress_latency_ms`: `-103`
  - `first_state`: `stand`

### Comparison

- Candidate beats deployed `main` by `845 ms` on startup classification latency (`2160 -> 1315 ms`).
- The remaining latency is now dominated by model/inference/classification stages rather than frame preprocessing.

## Pose Stage Timing Result

Candidate pose-stage trace summary from the same board run:

- `pixel_format`: `rgb`
- `width`: `640`
- `height`: `640`
- `samples`: `46`
- `avg_preprocess_ms`: `0.0`
- `avg_rknn_run_ms`: `24.93`
- `avg_total_ms`: `27.65`

Interpretation:

- The previous regression source (`health-falld` CPU-side preprocessing) is removed from the hot path.
- `health-falld` is now receiving the model-shaped RGB frames expected by the fast path.

## Conclusion

This change meets the current milestone:

- the new candidate is **not worse than `main`**
- the candidate is materially faster than `main` on first classification
- the pose preprocessing stage is effectively eliminated in the production path on board

The stricter future target of `<= 300 ms` from video-time event to classification is still not met; that requires a separate optimization round focused on the remaining inference/classification budget rather than image-format conversion.

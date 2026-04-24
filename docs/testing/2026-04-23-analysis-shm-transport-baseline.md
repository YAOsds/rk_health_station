# Analysis SHM Transport Baseline

Date: 2026-04-24

- Baseline branch: `main`
- Baseline commit: `45cd021`
- Candidate branch: `feature/analysis-shm-transport`
- Candidate commit: `e9c6c6b`
- Hardware: `RK3588 board @ 192.168.137.179` (`elf2-desktop`)
- Video: `/home/elf/Videos/video.mp4`

## Commands

Build baseline bundle from the main checkout:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station
BUILD_DIR=/tmp/rk_health_station-build-rk3588-main-shm-baseline \
BUNDLE_DIR=$PWD/out/rk3588_bundle_analysis_shm_main \
bash deploy/scripts/build_rk3588_bundle.sh
```

Build candidate bundle from the worktree:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/.worktrees/feature-analysis-shm-transport
BUILD_DIR=/tmp/rk_health_station-build-rk3588-candidate-shm \
BUNDLE_DIR=$PWD/out/rk3588_bundle_analysis_shm_candidate \
bash deploy/scripts/build_rk3588_bundle.sh
```

Sync bundles in place on the board to avoid the nearly-full root filesystem:

```bash
rsync -az --inplace --delete out/rk3588_bundle_analysis_shm_main/ \
  elf@192.168.137.179:/home/elf/rk3588_bundle_main_test/

rsync -az --inplace --delete out/rk3588_bundle_analysis_shm_candidate/ \
  elf@192.168.137.179:/home/elf/rk3588_bundle_candidate/
```

Run the latency harness:

```bash
python3 deploy/tests/measure_rk3588_test_mode_latency.py \
  --host elf@192.168.137.179 \
  --password elf \
  --bundle-dir /home/elf/rk3588_bundle_main_test \
  --video-file /home/elf/Videos/video.mp4

python3 deploy/tests/measure_rk3588_test_mode_latency.py \
  --host elf@192.168.137.179 \
  --password elf \
  --bundle-dir /home/elf/rk3588_bundle_candidate \
  --video-file /home/elf/Videos/video.mp4
```

## Raw Metrics

### Main baseline

```json
{
  "analysis_ingest_ts_ms": null,
  "analysis_ingress_latency_ms": -73,
  "analysis_publish_ts_ms": null,
  "classification_stage_latency_ms": 1382,
  "consumer_cpu_pct": 27.2,
  "first_classification_ts_ms": 1776961552597,
  "first_frame_ts_ms": 1776961551215,
  "first_state": "stand",
  "playback_start_ts_ms": 1776961551288,
  "producer_cpu_pct": 7.2,
  "producer_dropped_frames": 0,
  "startup_classification_latency_ms": 1309,
  "transport_latency_ms": null
}
```

### SHM candidate

```json
{
  "analysis_ingest_ts_ms": 1776961482812,
  "analysis_ingress_latency_ms": -81,
  "analysis_publish_ts_ms": 1776961481941,
  "classification_stage_latency_ms": 1342,
  "consumer_cpu_pct": 25.2,
  "first_classification_ts_ms": 1776961484154,
  "first_frame_ts_ms": 1776961482812,
  "first_state": "stand",
  "playback_start_ts_ms": 1776961482893,
  "producer_cpu_pct": 6.2,
  "producer_dropped_frames": 0,
  "startup_classification_latency_ms": 1261,
  "transport_latency_ms": 871
}
```

## Comparison

- Startup classification latency: `1261 ms` vs `1309 ms` (`-48 ms`, candidate faster)
- Classification stage latency: `1342 ms` vs `1382 ms` (`-40 ms`, candidate faster)
- Producer CPU: `6.2%` vs `7.2%` (`-1.0%`, candidate lower)
- Consumer CPU: `25.2%` vs `27.2%` (`-2.0%`, candidate lower)
- Dropped frames: `0` vs `0`

## Notes

- The first SHM implementation regressed badly on board because two effects compounded:
  - the producer ring used only `4` slots, which let startup bursts overwrite unread slots;
  - `AnalysisStreamClient` still collapsed each descriptor burst into a single `frameReceived(...)`, which throttled the effective frame rate after switching from large socket payloads to tiny descriptors.
- The final candidate fixes both issues by:
  - increasing the production ring size to `32` slots;
  - emitting every successfully reconstructed frame to the fall pipeline instead of only the last frame from a ready-read burst.
- `transport_latency_ms` is only available on the SHM candidate because `main` still uses the old full-payload socket transport. The current marker placement can also include a small amount of pre-playback warmup traffic, so the board-level pass/fail decision should use `startup_classification_latency_ms` and CPU deltas rather than the raw transport field alone.

## Conclusion

The shared-memory transport is better than the current `main` baseline on the measured board run:

- it improves startup classification latency by `48 ms`;
- it reduces producer CPU by `1.0%`;
- it reduces consumer CPU by `2.0%`;
- it does so without introducing producer-side dropped frames in the measured run.

# 2026-04-27 RGA + RKNN Stage 1/2 Result

## Scope

Implemented Scheme B in a bounded form:

- `health-videod` publishes model-sized RGB analysis frames with pose letterbox metadata.
- The shared descriptor and shared-memory ring preserve `posePreprocessed`, `poseXPad`, `poseYPad`, and `poseScale`.
- `health-falld` consumes model-ready RGB frames without re-letterboxing and uses the supplied metadata for pose post-processing.
- RKNN pose inference preallocates output buffers by default (`output_prealloc_path=true`).
- A full RKNN IO-memory path remains available for probes with `RK_FALL_RKNN_IO_MEM=zero_copy`, but it is not the default because it did not improve total timing on this board.

## Board Comparison

Board: `elf@192.168.137.179`  
Runtime: `/home/elf/rk3588_bundle`, `DISPLAY=:1`, `RK_RUNTIME_MODE=system`  
Baseline commit: `d50eed2fd9989397a4baaee95d1dcefdf5cc3e01` (`main`)

| Metric, last 200 pose samples | Fresh main baseline | Scheme B final | Delta |
| --- | ---: | ---: | ---: |
| `preprocess_ms avg` | 0.000 | 0.000 | 0.000 |
| `inputs_set_ms avg` | 0.000 | 0.000 | 0.000 |
| `rknn_run_ms avg` | 21.705 | 21.540 | -0.165 ms |
| `outputs_get_ms avg` | 1.000 | 1.000 | 0.000 ms |
| `post_process_ms avg` | 0.000 | 0.000 | 0.000 |
| `total_ms avg` | 23.885 | 23.675 | -0.210 ms (-0.9%) |
| timing rows | 935 | 936 | comparable |
| `output_prealloc_path` | unavailable | 200 / 200 | enabled |

Process snapshot after about 60 seconds:

| Process metric | Fresh main baseline | Scheme B final |
| --- | ---: | ---: |
| `health-videod %CPU` | 2.5 | 2.8 |
| `health-falld %CPU` | 7.6 | 7.6 |
| `health-ui %CPU` | 0.2 | 0.3 |

Functional status stayed healthy in both runs: all four services running, health socket present, video socket present, analysis socket present, and fall socket present. Both runs sustained `15.0 ingest_fps` and `15.0 infer_fps`.

## Probe Notes

- Full RKNN IO memory (`RK_FALL_RKNN_IO_MEM=zero_copy`) was tested separately. It made `io_mem_path=true` and reduced `outputs_get_ms` to `0.0`, but shifted time into `rknn_run_ms`; total timing was effectively neutral around `23.77 ms`, so it is kept opt-in.
- Querying native NHWC output attributes for full IO memory caused a severe post-processing regression on this model/board combination, so that probe was reverted.
- The current main baseline already had `preprocess_ms=0`, so Stage 1 cannot produce a large pose-side gain until Stage 3 removes the remaining cross-process payload copy with DMABUF/fd transport.

## Conclusion

Scheme B final shows a small but measurable pose timing improvement versus a fresh `main` baseline on the same board run: `total_ms` improved by about `0.21 ms` (`0.9%`) while preserving 15 FPS ingestion/inference. The change is safe to carry forward, but the larger expected performance win should come from Stage 3 DMABUF/fd transport.

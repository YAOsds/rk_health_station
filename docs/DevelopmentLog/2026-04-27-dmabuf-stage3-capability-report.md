# 2026-04-27 DMABUF Stage 3 Capability Report

## Board

- Host: `elf2-desktop`
- Kernel: `Linux 5.10.209 aarch64`
- User groups include `video`, so `/dev/rga`, `/dev/video*`, and public dma-heap nodes are accessible without sudo.

## Device Nodes

Available:

- `/dev/rga` owned by `root:video`, mode `crw-rw----`
- `/dev/video11` active camera path, driver `rkisp_v6`, card `rkisp_mainpath`
- `/dev/dma_heap/system-dma32`, `/dev/dma_heap/system-uncached`, `/dev/dma_heap/system-uncached-dma32` open read/write as user `elf`
- `/dev/dma_heap/cma`, `/dev/dma_heap/cma-uncached`, `/dev/dma_heap/system` are root-only and not usable without permission changes
- `/dev/dri/renderD128` and `/dev/dri/renderD129` exist but user `elf` is not in `render`; not needed for the first RGA/RKNN probe

## Camera / V4L2

`/dev/video11` reports:

- Driver: `rkisp_v6`
- Device caps: `Video Capture Multiplanar`, `Streaming`, `Extended Pix Format`
- Current format: `640x480`, `NM12`, two planes
- Enumerated formats include `NV12`, `NV21`, `NM12`, `NM21`, `NV16`, `NV61`, and `UYVY`

## GStreamer

`v4l2src` supports the required IO modes:

- `io-mode=dmabuf`
- `io-mode=dmabuf-import`
- `io-mode=mmap`

Smoke tests after stopping the running bundle:

| Pipeline | Result |
| --- | --- |
| `v4l2src io-mode=dmabuf num-buffers=5 ! ... ! fakesink` | exit `0` |
| `v4l2src io-mode=mmap num-buffers=5 ! ... ! fakesink` | exit `0` |
| `v4l2src io-mode=dmabuf-import num-buffers=5 ! ... ! fakesink` | exit `1`, expected without downstream pool |

Implication: capture-side DMABUF export is available. The current `gst-launch + fdsink fd=1` topology still cannot transfer DMABUF fds through stdout; production fd passing will need in-process GStreamer or a helper process that uses Unix-domain fd passing.

## RGA

Headers in the cross sysroot expose fd-backed APIs:

- `importbuffer_fd(...)`
- `releasebuffer_handle(...)`
- `wrapbuffer_fd(...)`
- `wrapbuffer_handle(...)`

Current production code still uses `wrapbuffer_virtualaddr(...)` in `RgaFrameConverter`, so the next probe should exercise `dma_heap -> importbuffer_fd/wrapbuffer_fd -> imcheck/imcopy/improcess` on the board.

## RKNN

Board runtime libraries export required fd/IO-memory symbols:

- `rknn_create_mem_from_fd`
- `rknn_set_io_mem`
- `rknn_mem_sync`

Checked in:

- `/usr/lib/librknnrt.so`
- `/usr/lib64/librknnrt.so`
- `/home/elf/rk3588_bundle/lib/app/librknnrt.so`

Implication: RKNN fd import is technically available, but prior full IO-memory timing was neutral/regressive, so RKNN fd import must be measured in a sidecar probe before it becomes production default.

## Recommendation

Proceed with a sidecar RGA fd probe first:

1. Allocate RGB buffers from `/dev/dma_heap/system-uncached-dma32` or `/dev/dma_heap/system-dma32`.
2. Import/wrap them through RGA fd APIs.
3. Run a simple `imfill` / `imcopy` / resize operation and verify output bytes through `mmap`.
4. Only after that succeeds, add RKNN fd-import timing against the pose model.

Do not modify the production shared-memory frame transport yet.

## Sidecar RGA DMABUF Probe Result

Added a sidecar-only probe source and build script:

- `deploy/probes/rga_dmabuf_probe.cpp`
- `deploy/scripts/build_rga_dmabuf_probe.sh`

The probe allocates two RGB888 DMA buffers, imports them with RGA fd APIs, runs `imcopy`, and verifies the copied bytes through `mmap`.

Board results:

| Heap | Result | Interpretation |
| --- | --- | --- |
| `/dev/dma_heap/system-uncached-dma32` | `rga_dmabuf_probe_ok`, exit `0` | Recommended first production/probe heap |
| `/dev/dma_heap/system-uncached` | `rga_dmabuf_probe_ok`, exit `0` | Also viable |
| `/dev/dma_heap/system-dma32` | verify mismatch at byte `0`, exit `7` | Cache coherency/sync issue for CPU-written cached memory in this simple probe |

Conclusion: RGA fd-backed DMABUF operation is verified on the board. The first real probe should use an uncached heap unless explicit cache synchronization is added for cached heaps.

## Sidecar RKNN DMABUF Input Probe Result

Added a sidecar-only RKNN input probe:

- `deploy/probes/rknn_dmabuf_input_probe.cpp`

The probe loads the pose RKNN model, allocates a DMA buffer for the RGB input tensor, imports it with `rknn_create_mem_from_fd`, binds it with `rknn_set_io_mem`, runs 30 inferences on a constant input, and uses normal `rknn_outputs_get` for outputs.

Board results while the production bundle was stopped:

| Heap | Result | `avg_run_ms` | `avg_outputs_get_ms` | Interpretation |
| --- | --- | ---: | ---: | --- |
| `/dev/dma_heap/system-uncached-dma32` | exit `0` | `25.129` | `1.592` | Works, but slower than production timing |
| `/dev/dma_heap/system-uncached` | exit `0` | `23.667` | `1.546` | Works, closest candidate among tested heaps |
| `/dev/dma_heap/system-dma32` | exit `0` | `24.157` | `1.545` | Works with explicit `rknn_mem_sync`, but RGA copy probe showed CPU/RGA coherency risk |

Production Stage 1/2 timing for comparison was around `rknn_run_ms avg 21.54` and `outputs_get_ms avg 1.0` in the integrated service. Therefore RKNN input DMABUF import is functionally available, but it should not be assumed faster than the current input path. The value of DMABUF remains strongest for removing cross-process payload copies and improving 25 FPS stability, not for making RKNN compute itself faster.

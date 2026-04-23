# Analysis Shared-Memory Transport Design

## Goal

Replace the current large-frame analysis transport path between `health_videod` and `health_falld` with a shared-memory ring buffer while keeping a Unix domain socket as the notification/control channel. The new design reduces copies, removes large payload transfer over the socket, preserves the current "latest frame wins" behavior, and does not include a rollback or dual-transport compatibility mode.

## Current State

The current analysis chain works like this:

1. `GstreamerVideoPipelineBackend` runs `gst-launch` as a subprocess.
2. The analysis branch emits fixed-size RGB frames to `fdsink fd=1`.
3. `health_videod` reads subprocess `stdout`, reconstructs whole frames, and stores them in `AnalysisFramePacket::payload`.
4. `GstreamerAnalysisOutputBackend` serializes each packet and writes the full payload to a `QLocalSocket`.
5. `health_falld` reads the socket, deserializes the packet, and forwards it to pose inference.

This path performs unnecessary copies and serializes large frame payloads through the socket even though the runtime semantics only care about the newest frame.

## Constraints

- Keep the existing `gst-launch` subprocess approach for now.
- Keep Unix domain sockets for connection management and frame-arrival notification.
- Do not introduce a rollback path, feature flag, or socket-payload fallback.
- Preserve the current upper-layer API in `health_falld`: inference still receives `AnalysisFramePacket`.
- Prefer dropping stale frames over blocking the producer.
- Scope the transport redesign to the analysis IPC boundary; avoid refactoring inference and tracking layers.

## Decision Summary

Use a per-camera POSIX shared-memory ring buffer for frame payloads and a lightweight Unix socket message carrying only frame descriptors. `health_videod` writes raw frame bytes into shared memory, then sends a descriptor over the socket. `health_falld` receives the descriptor, validates the referenced slot, reconstructs an `AnalysisFramePacket` from shared memory, and emits it to the existing processing pipeline.

## Architecture

### Producer Side

`health_videod` keeps its current GStreamer subprocess and `stdout` read path. After assembling a complete RGB frame from `stdout`, it writes the frame into a shared-memory slot rather than embedding the payload into a socket packet.

The producer side consists of:

- `GstreamerVideoPipelineBackend`: still owns subprocess startup and `stdout` frame assembly.
- `SharedMemoryFrameRingWriter`: new component that owns `shm_open`, `ftruncate`, `mmap`, slot layout, and write-side sequencing.
- `GstreamerAnalysisOutputBackend`: changed to send only lightweight descriptors over `QLocalSocket`.

### Consumer Side

`health_falld` keeps the existing socket connection model. The socket now carries a descriptor instead of a full frame payload. The consumer uses that descriptor to read the frame from shared memory and then emits a normal `AnalysisFramePacket` upstream.

The consumer side consists of:

- `AnalysisStreamClient`: still owns socket connection, reconnect, and read buffering, but now decodes descriptors.
- `SharedMemoryFrameRingReader`: new component that opens/maps the producer ring and reconstructs `AnalysisFramePacket`.
- Existing pose / tracking / classification layers remain unchanged.

## Shared Memory Layout

Each camera gets its own POSIX shared-memory object named from the camera id, for example:

- `/rk_video_analysis_front_cam`

The mapped region is organized as:

```text
[SharedHeader][Slot 0][Slot 1]...[Slot N-1]
```

The global header stores stable layout metadata:

```cpp
struct SharedHeader {
    quint32 magic;
    quint16 version;
    quint16 slotCount;
    quint32 slotStride;
    quint32 maxFrameBytes;
    quint32 producerPid;
    quint64 publishedFrames;
    quint64 droppedFrames;
};
```

Each slot has a fixed-size header followed by a fixed-size byte region:

```cpp
struct FrameSlotHeader {
    quint64 sequence;
    quint64 frameId;
    qint64 timestampMs;
    qint32 width;
    qint32 height;
    qint32 pixelFormat;
    quint32 payloadBytes;
    quint32 flags;
};
```

Each slot is laid out as:

```text
[FrameSlotHeader][raw frame bytes padded to slotStride]
```

### Layout Rules

- `slotStride` is fixed for the entire ring.
- `maxFrameBytes` is fixed for the entire ring.
- The initial implementation targets the current producer output: `RGB 640x640`, so `maxFrameBytes = 640 * 640 * 3`.
- `slotCount` defaults to `4`.
- The producer uses a simple circular overwrite policy. When the ring wraps, the oldest unread slot may be overwritten.

## Descriptor Protocol

The Unix socket no longer carries frame payload bytes. It carries only a lightweight descriptor:

```cpp
struct AnalysisFrameDescriptor {
    quint64 frameId;
    qint64 timestampMs;
    QString cameraId;
    qint32 width;
    qint32 height;
    AnalysisPixelFormat pixelFormat;
    quint32 slotIndex;
    quint64 sequence;
    quint32 payloadBytes;
};
```

This message is enough for the consumer to locate and validate the corresponding frame in shared memory.

### Protocol Rules

- Descriptor messages use a dedicated encoder/decoder rather than reusing `analysis_stream_protocol`.
- `analysis_stream_protocol` remains the internal full-frame packet representation used above the ingest boundary.
- The descriptor protocol must be versioned independently from the old packet transport.
- Descriptor delivery is best-effort. A descriptor may legitimately reference a slot that has already been overwritten by the time the consumer reads it; that frame is dropped.

## Synchronization Model

The transport is optimized for one producer and one primary consumer per camera, without blocking the producer.

### Producer Write Order

For each new frame:

1. Select the next slot in round-robin order.
2. Copy payload bytes into the slot's byte region.
3. Write the non-sequence slot header fields.
4. Publish the final `sequence` value last.
5. Send the descriptor over the Unix socket.

### Consumer Read Validation

For each descriptor:

1. Read the slot header `sequence`.
2. If it does not match the descriptor `sequence`, drop the frame.
3. Copy header fields and payload bytes into a local `AnalysisFramePacket`.
4. Re-read `sequence`.
5. If the two sequence reads differ, drop the frame because the slot was rewritten mid-read.

This avoids per-frame locks and is consistent with the current "latest frame wins" semantics.

## Data Flow

### Producer Path

1. `GstreamerVideoPipelineBackend` receives bytes from subprocess `stdout`.
2. Once one RGB frame has been assembled, it creates a producer-side frame view.
3. `SharedMemoryFrameRingWriter` writes the frame into the next slot and returns:
   - `slotIndex`
   - `sequence`
   - `payloadBytes`
4. `GstreamerAnalysisOutputBackend` sends an `AnalysisFrameDescriptor` to connected clients.

### Consumer Path

1. `AnalysisStreamClient` receives a descriptor from the socket.
2. `SharedMemoryFrameRingReader` opens/maps the named ring if needed.
3. The reader validates the slot using `slotIndex + sequence`.
4. If valid, it reconstructs an `AnalysisFramePacket`.
5. `AnalysisStreamClient` emits `frameReceived(const AnalysisFramePacket&)`.

### Runtime Semantics

- Slow consumers may miss frames.
- The newest valid frame is preferred over ordered delivery guarantees.
- Producer throughput must not depend on consumer throughput.

## Component Changes

### New Files

- `rk_app/src/health_videod/analysis/shared_memory_frame_ring.h`
- `rk_app/src/health_videod/analysis/shared_memory_frame_ring.cpp`
- `rk_app/src/health_falld/ingest/shared_memory_frame_reader.h`
- `rk_app/src/health_falld/ingest/shared_memory_frame_reader.cpp`
- `rk_app/src/shared/protocol/analysis_frame_descriptor_protocol.h`
- `rk_app/src/shared/protocol/analysis_frame_descriptor_protocol.cpp`

### Modified Files

- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- `rk_app/src/health_videod/analysis/analysis_output_backend.h`
- `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h`
- `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`
- `rk_app/src/health_falld/ingest/analysis_stream_client.h`
- `rk_app/src/health_falld/ingest/analysis_stream_client.cpp`
- `rk_app/src/health_falld/runtime/runtime_config.h`
- `rk_app/src/health_falld/runtime/runtime_config.cpp`
- `rk_app/src/shared/models/fall_models.h`
- relevant `CMakeLists.txt` files in `rk_app/src/shared`, `rk_app/src/health_videod`, `rk_app/src/health_falld`, and `rk_app/src/tests`

## Model Changes

Introduce a dedicated descriptor model instead of overloading `AnalysisFramePacket`:

```cpp
struct AnalysisFrameDescriptor {
    quint64 frameId = 0;
    qint64 timestampMs = 0;
    QString cameraId;
    qint32 width = 0;
    qint32 height = 0;
    AnalysisPixelFormat pixelFormat = AnalysisPixelFormat::Jpeg;
    quint32 slotIndex = 0;
    quint64 sequence = 0;
    quint32 payloadBytes = 0;
};
```

`AnalysisFramePacket` remains unchanged and continues to represent a fully materialized frame for upper layers.

## Writer Responsibilities

`SharedMemoryFrameRingWriter` is responsible for:

- creating/opening the named shared-memory object
- sizing and mapping the region
- initializing the header
- validating payload size against `maxFrameBytes`
- selecting the next slot
- writing the slot payload and header
- tracking producer-side counts such as `publishedFrames` and `droppedFrames`
- exposing enough metadata for `GstreamerAnalysisOutputBackend` to build descriptors

The writer must not depend on `QLocalSocket` or descriptor encoding.

## Reader Responsibilities

`SharedMemoryFrameRingReader` is responsible for:

- deriving the shared-memory name from the camera id
- opening and mapping the region lazily
- validating header magic/version/layout
- validating slot index, payload size, frame shape, and sequence
- reconstructing an `AnalysisFramePacket`
- remapping when producer identity or layout changes

The reader must not know about pose inference; it stops at producing a valid `AnalysisFramePacket`.

## Runtime Configuration

The first implementation uses fixed defaults:

- shm name derived from camera id
- `slotCount = 4`
- `maxFrameBytes = 640 * 640 * 3`

Add explicit config fields only where runtime discovery is necessary, such as:

- optional shared-memory name override in `FallRuntimeConfig`

Do not add a transport mode switch because rollback is out of scope.

## Error Handling

### Producer Errors

- If shared memory cannot be created or mapped, mark analysis output unavailable and surface an error in `AnalysisChannelStatus::lastError`.
- If a frame exceeds `maxFrameBytes`, drop it and increment `droppedFrames`.
- If no socket clients are connected, producer may continue writing to shared memory, but descriptor publication becomes a no-op.

### Consumer Errors

- If the shared-memory object does not exist yet, wait for later descriptors and retry lazily.
- If header magic/version/layout is invalid, mark input disconnected and retry on subsequent descriptors.
- If the referenced slot has already been overwritten, drop the frame silently.
- If payload size or pixel-format metadata is invalid, drop the frame and surface a non-fatal runtime error.

### System Behavior

- Frame loss is acceptable.
- Producer blocking is not acceptable.
- A single corrupt frame must not tear down the whole ingest pipeline.

## Multi-Consumer Behavior

The transport is optimized around one analysis consumer, but multiple socket clients may still connect. Shared memory is read-only for consumers, so multiple readers are safe as long as each reader accepts possible frame overwrite. No per-consumer acknowledgment or reference counting is added in this design.

## Testing Strategy

### Unit Tests

- shared-memory writer creates and initializes the ring correctly
- reader validates magic/version/layout
- writer/reader round-trip one frame successfully
- descriptor pointing to an overwritten slot is rejected
- sequence mismatch during read causes frame drop
- invalid payload size is rejected

### Protocol Tests

- descriptor encode/decode round-trip
- malformed descriptor rejection
- invalid slot index rejection

### Integration Tests

- `health_videod` writes a frame to shared memory and publishes a descriptor
- `health_falld` receives the descriptor and reconstructs a matching `AnalysisFramePacket`
- slow-consumer scenario proves old descriptors can be dropped without blocking
- producer restart/remap scenario proves the reader can recover

### Regression Tests

- update `analysis_stream_client_test` to the descriptor + shm model
- update video daemon analysis backend tests to verify descriptor publication instead of full-payload publication
- keep end-to-end fall daemon tests asserting that inference still receives correct RGB frames

### Performance Baseline

Performance validation is a required part of this design, not an optional follow-up. The transport change is intended to improve runtime behavior relative to the current socket-payload transport on the current `main` branch, so testing must include an explicit before/after comparison.

Use the current `main` branch as the baseline implementation and compare it against the shared-memory design implementation under the same input conditions. The comparison should use the same RK3588 hardware, the same test media, the same camera/test-mode settings, and the same model assets.

At minimum, collect and compare:

- end-to-end frame delivery latency from `health_videod` publish point to `health_falld` ingest point
- first-frame-to-first-inference latency in test mode
- steady-state analysis FPS observed by `health_falld`
- producer-side dropped-frame count
- producer CPU usage for `health-videod`
- consumer CPU usage for `health_falld`

Where available, also record pose-stage timings already exposed by the runtime, especially preprocess time. Because the new design removes large socket payload transport, the expected result is that the shared-memory implementation is no worse than `main` on all headline metrics and measurably better on at least frame-delivery overhead, dropped-frame behavior under load, or CPU cost.

The performance test plan should produce a checked-in result note documenting:

- baseline commit on `main`
- test commit for the shared-memory design
- hardware and runtime environment
- commands used to run the comparison
- raw measurements
- a short conclusion stating whether the design delivered the expected improvement

## Migration Plan

1. Add shared-memory ring and descriptor protocol primitives with tests.
2. Integrate the writer into `health_videod` after `stdout` frame assembly.
3. Change `GstreamerAnalysisOutputBackend` to publish descriptors only.
4. Integrate the reader into `health_falld` and keep `frameReceived(AnalysisFramePacket)` unchanged.
5. Update integration and end-to-end tests.
6. Remove any now-dead assumptions that the analysis socket carries full frame payloads.

## Non-Goals

- replacing `gst-launch` with in-process GStreamer
- DMA-BUF or kernel-level zero-copy
- transport rollback, dual-stack support, or runtime transport selection
- changing upper-layer pose/action inference interfaces
- guaranteed delivery of every analysis frame

## Open Choices Already Resolved

- Keep Unix socket as control/notification path: yes.
- Move only frame payload bytes to shared memory: yes.
- Preserve "latest wins" semantics: yes.
- Exclude rollback path: yes.
- Keep inference-side API stable: yes.

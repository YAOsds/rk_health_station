# Analysis Shared-Memory Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the full-payload analysis socket transport with a shared-memory ring buffer plus lightweight descriptor notifications while preserving the existing `AnalysisFramePacket` ingest API and producing a measurable performance win against the current `main` branch.

**Architecture:** Keep the existing `gst-launch` subprocess and Unix domain socket connection model. `health-videod` writes RGB analysis frames into a per-camera POSIX shared-memory ring and sends only an `AnalysisFrameDescriptor` over the socket; `health-falld` resolves the descriptor through a shared-memory reader, reconstructs an `AnalysisFramePacket`, and feeds the unchanged inference pipeline. Add performance markers and a comparison harness so the new transport is evaluated against the current `main` baseline on RK3588 hardware.

**Tech Stack:** C++17, Qt 5/6 Core/Network/Test, POSIX `shm_open`/`mmap`, existing local-socket IPC, Qt Test/CTest, Python 3 benchmark harness.

---

## File Structure Map

### Shared models and protocol files
- Modify: `rk_app/src/shared/models/fall_models.h` - add the descriptor model that names a shared-memory slot without embedding frame bytes.
- Create: `rk_app/src/shared/protocol/analysis_frame_descriptor_protocol.h` - declare the descriptor codec API.
- Create: `rk_app/src/shared/protocol/analysis_frame_descriptor_protocol.cpp` - implement framing, version checks, and validation for descriptor messages.
- Modify: `rk_app/src/shared/CMakeLists.txt` - compile the new descriptor protocol into `rk_shared`.
- Create: `rk_app/src/tests/protocol_tests/analysis_frame_descriptor_protocol_test.cpp` - unit-test descriptor encode/decode and invalid-input rejection.

### Shared-memory ring primitives
- Create: `rk_app/src/health_videod/analysis/shared_memory_frame_ring.h` - declare the shared-memory layout, writer result metadata, and helper constants.
- Create: `rk_app/src/health_videod/analysis/shared_memory_frame_ring.cpp` - implement the producer-side ring mapping and write path.
- Create: `rk_app/src/health_falld/ingest/shared_memory_frame_reader.h` - declare the reader-side API that reconstructs `AnalysisFramePacket` from a descriptor.
- Create: `rk_app/src/health_falld/ingest/shared_memory_frame_reader.cpp` - implement mapping, validation, sequence checks, and packet reconstruction.
- Create: `rk_app/src/tests/video_daemon_tests/shared_memory_frame_ring_test.cpp` - cover writer layout, wrap-around, and overwrite behavior.
- Create: `rk_app/src/tests/fall_daemon_tests/shared_memory_frame_reader_test.cpp` - cover descriptor validation, remap, and overwritten-slot rejection.

### `health-videod` producer integration
- Modify: `rk_app/src/health_videod/analysis/analysis_output_backend.h` - change the publication contract from full packets to descriptors and expose shared-memory metadata if needed.
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h` - store descriptor publication state and dropped-frame counters.
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp` - send descriptor messages to socket clients instead of frame payloads.
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h` - hold the ring writer and carry writer results alongside assembled stdout frames.
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp` - write frames into shared memory, generate descriptors, and emit publish markers.
- Modify: `rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp` - assert descriptor publication rather than full-payload publication.
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp` - assert `stdout`-assembled RGB frames are written to the ring and published as descriptors.
- Modify: `rk_app/src/tests/video_daemon_tests/video_service_analysis_test.cpp` - keep service-level analysis lifecycle coverage valid after the transport swap.
- Modify: `rk_app/src/health_videod/CMakeLists.txt` - compile and link the new ring helper.

### `health-falld` consumer integration
- Modify: `rk_app/src/health_falld/ingest/analysis_stream_client.h` - hold the reader and consume descriptors rather than payload packets.
- Modify: `rk_app/src/health_falld/ingest/analysis_stream_client.cpp` - decode descriptor messages, resolve frames from shared memory, and preserve `frameReceived(AnalysisFramePacket)`.
- Modify: `rk_app/src/health_falld/runtime/runtime_config.h` - add optional shared-memory name override if needed.
- Modify: `rk_app/src/health_falld/runtime/runtime_config.cpp` - load the override from the environment.
- Modify: `rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp` - replace socket-payload assumptions with descriptor+shared-memory tests.
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp` - update the fake analysis server path to publish descriptors and shared-memory slots.
- Modify: `rk_app/src/health_falld/CMakeLists.txt` - compile and link the reader helper.

### Performance baseline and reporting
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp` - emit transport publish markers and surface dropped-frame counts.
- Modify: `rk_app/src/health_falld/ingest/analysis_stream_client.cpp` - emit transport ingest markers for frame-delivery latency measurement.
- Modify: `deploy/tests/measure_rk3588_test_mode_latency.py` - collect transport metrics for both baseline `main` and the shared-memory implementation.
- Modify: `deploy/tests/measure_rk3588_test_mode_latency_test.py` - unit-test the new metric computation and result comparison helpers.
- Create: `docs/testing/2026-04-23-analysis-shm-transport-baseline.md` - record the `main` baseline commit, candidate commit, commands, raw measurements, and conclusion.
- Modify: `rk_app/src/tests/CMakeLists.txt` - register all new transport tests.

---

### Task 1: Add the descriptor model and its codec before touching shared memory

**Files:**
- Modify: `rk_app/src/shared/models/fall_models.h`
- Create: `rk_app/src/shared/protocol/analysis_frame_descriptor_protocol.h`
- Create: `rk_app/src/shared/protocol/analysis_frame_descriptor_protocol.cpp`
- Modify: `rk_app/src/shared/CMakeLists.txt`
- Create: `rk_app/src/tests/protocol_tests/analysis_frame_descriptor_protocol_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing descriptor protocol test**

```cpp
#include "models/fall_models.h"
#include "protocol/analysis_frame_descriptor_protocol.h"

#include <QtTest/QTest>

class AnalysisFrameDescriptorProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsDescriptor();
    void rejectsInvalidSlotIndex();
};

void AnalysisFrameDescriptorProtocolTest::roundTripsDescriptor() {
    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 77;
    descriptor.timestampMs = 1777000000123;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 640;
    descriptor.height = 640;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.slotIndex = 2;
    descriptor.sequence = 18;
    descriptor.payloadBytes = 640 * 640 * 3;

    const QByteArray encoded = encodeAnalysisFrameDescriptor(descriptor);
    AnalysisFrameDescriptor decoded;
    QVERIFY(decodeAnalysisFrameDescriptor(encoded, &decoded));
    QCOMPARE(decoded.slotIndex, 2u);
    QCOMPARE(decoded.sequence, 18u);
    QCOMPARE(decoded.payloadBytes, 640 * 640 * 3u);
}

void AnalysisFrameDescriptorProtocolTest::rejectsInvalidSlotIndex() {
    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 88;
    descriptor.timestampMs = 1777000000222;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 640;
    descriptor.height = 640;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.slotIndex = 9999;
    descriptor.sequence = 1;
    descriptor.payloadBytes = 640 * 640 * 3;

    QByteArray encoded = encodeAnalysisFrameDescriptor(descriptor);
    AnalysisFrameDescriptor decoded;
    QVERIFY(!decodeAnalysisFrameDescriptor(encoded, &decoded));
}

QTEST_MAIN(AnalysisFrameDescriptorProtocolTest)
#include "analysis_frame_descriptor_protocol_test.moc"
```

- [ ] **Step 2: Register the new test target and run it to prove it fails first**

```cmake
add_executable(analysis_frame_descriptor_protocol_test
    protocol_tests/analysis_frame_descriptor_protocol_test.cpp
)

set_target_properties(analysis_frame_descriptor_protocol_test PROPERTIES
    AUTOMOC ON
)

target_link_libraries(analysis_frame_descriptor_protocol_test PRIVATE
    rk_shared
    ${RK_QT_TEST_TARGET}
)

add_test(NAME analysis_frame_descriptor_protocol_test COMMAND analysis_frame_descriptor_protocol_test)
```

Run: `cmake --build out/build-rk_app-host -j4 --target analysis_frame_descriptor_protocol_test && ctest --test-dir out/build-rk_app-host -R analysis_frame_descriptor_protocol_test --output-on-failure`
Expected: build fails because `AnalysisFrameDescriptor` and its codec do not exist yet.

- [ ] **Step 3: Add the descriptor model to the shared fall model header**

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

Q_DECLARE_METATYPE(AnalysisFrameDescriptor)
```

- [ ] **Step 4: Implement the descriptor codec with framing and validation**

```cpp
// analysis_frame_descriptor_protocol.h
QByteArray encodeAnalysisFrameDescriptor(const AnalysisFrameDescriptor &descriptor);
bool decodeAnalysisFrameDescriptor(const QByteArray &bytes, AnalysisFrameDescriptor *descriptor);
bool takeFirstAnalysisFrameDescriptor(QByteArray *bytes, AnalysisFrameDescriptor *descriptor);
```

```cpp
// analysis_frame_descriptor_protocol.cpp
namespace {
constexpr quint32 kDescriptorMagic = 0x524B4144; // RKAD
constexpr quint16 kDescriptorVersion = 1;
constexpr quint32 kMaxDescriptorSlotIndex = 64;

bool hasValidDescriptorShape(const AnalysisFrameDescriptor &descriptor) {
    if (descriptor.cameraId.isEmpty() || descriptor.width <= 0 || descriptor.height <= 0) {
        return false;
    }
    if (descriptor.slotIndex >= kMaxDescriptorSlotIndex || descriptor.sequence == 0) {
        return false;
    }
    switch (descriptor.pixelFormat) {
    case AnalysisPixelFormat::Rgb:
        return descriptor.payloadBytes == static_cast<quint32>(descriptor.width * descriptor.height * 3);
    case AnalysisPixelFormat::Nv12:
        return descriptor.payloadBytes == static_cast<quint32>(descriptor.width * descriptor.height * 3 / 2);
    case AnalysisPixelFormat::Jpeg:
        return descriptor.payloadBytes > 0;
    }
    return false;
}
}
```

- [ ] **Step 5: Link the codec into `rk_shared` and run the focused test until it passes**

```cmake
add_library(rk_shared STATIC
    debug/latency_marker_writer.cpp
    protocol/device_frame.cpp
    protocol/ipc_message.cpp
    protocol/analysis_stream_protocol.cpp
    protocol/analysis_frame_descriptor_protocol.cpp
    protocol/fall_ipc.cpp
    protocol/video_ipc.cpp
    security/hmac_helper.cpp
    storage/database.cpp
)
```

Run: `cmake --build out/build-rk_app-host -j4 --target analysis_frame_descriptor_protocol_test && ctest --test-dir out/build-rk_app-host -R analysis_frame_descriptor_protocol_test --output-on-failure`
Expected: `analysis_frame_descriptor_protocol_test` passes.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/shared/models/fall_models.h \
        rk_app/src/shared/protocol/analysis_frame_descriptor_protocol.h \
        rk_app/src/shared/protocol/analysis_frame_descriptor_protocol.cpp \
        rk_app/src/shared/CMakeLists.txt \
        rk_app/src/tests/protocol_tests/analysis_frame_descriptor_protocol_test.cpp \
        rk_app/src/tests/CMakeLists.txt
git commit -m "refactor: add analysis frame descriptor protocol"
```

### Task 2: Build the shared-memory ring writer and reader with isolated tests

**Files:**
- Create: `rk_app/src/health_videod/analysis/shared_memory_frame_ring.h`
- Create: `rk_app/src/health_videod/analysis/shared_memory_frame_ring.cpp`
- Create: `rk_app/src/health_falld/ingest/shared_memory_frame_reader.h`
- Create: `rk_app/src/health_falld/ingest/shared_memory_frame_reader.cpp`
- Create: `rk_app/src/tests/video_daemon_tests/shared_memory_frame_ring_test.cpp`
- Create: `rk_app/src/tests/fall_daemon_tests/shared_memory_frame_reader_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Modify: `rk_app/src/health_videod/CMakeLists.txt`
- Modify: `rk_app/src/health_falld/CMakeLists.txt`

- [ ] **Step 1: Write the failing writer and reader tests**

```cpp
void SharedMemoryFrameRingTest::writesRgbFrameIntoNextSlot() {
    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 4, 640 * 640 * 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket frame;
    frame.frameId = 101;
    frame.timestampMs = 1777000001111;
    frame.cameraId = QStringLiteral("front_cam");
    frame.width = 640;
    frame.height = 640;
    frame.pixelFormat = AnalysisPixelFormat::Rgb;
    frame.payload = QByteArray(640 * 640 * 3, '\x5a');

    const auto result = writer.publish(frame);
    QCOMPARE(result.slotIndex, 0u);
    QCOMPARE(result.sequence, 1u);
}
```

```cpp
void SharedMemoryFrameReaderTest::rejectsDescriptorForOverwrittenSlot() {
    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 1, 4 * 4 * 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket first;
    first.frameId = 1;
    first.timestampMs = 10;
    first.cameraId = QStringLiteral("front_cam");
    first.width = 4;
    first.height = 4;
    first.pixelFormat = AnalysisPixelFormat::Rgb;
    first.payload = QByteArray(4 * 4 * 3, '\x11');

    const auto firstResult = writer.publish(first);

    AnalysisFramePacket second = first;
    second.frameId = 2;
    second.payload = QByteArray(4 * 4 * 3, '\x22');
    writer.publish(second);

    SharedMemoryFrameReader reader;
    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = first.frameId;
    descriptor.timestampMs = first.timestampMs;
    descriptor.cameraId = first.cameraId;
    descriptor.width = first.width;
    descriptor.height = first.height;
    descriptor.pixelFormat = first.pixelFormat;
    descriptor.slotIndex = firstResult.slotIndex;
    descriptor.sequence = firstResult.sequence;
    descriptor.payloadBytes = first.payload.size();

    AnalysisFramePacket decoded;
    QString error;
    QVERIFY(!reader.read(descriptor, &decoded, &error));
    QCOMPARE(error, QStringLiteral("analysis_slot_overwritten"));
}
```

- [ ] **Step 2: Register the tests and run them to prove the ring does not exist yet**

```cmake
add_executable(shared_memory_frame_ring_test
    video_daemon_tests/shared_memory_frame_ring_test.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
)

add_executable(shared_memory_frame_reader_test
    fall_daemon_tests/shared_memory_frame_reader_test.cpp
    ../health_videod/analysis/shared_memory_frame_ring.cpp
    ../health_falld/ingest/shared_memory_frame_reader.cpp
)
```

Run: `cmake --build out/build-rk_app-host -j4 --target shared_memory_frame_ring_test shared_memory_frame_reader_test && ctest --test-dir out/build-rk_app-host -R "shared_memory_frame_ring_test|shared_memory_frame_reader_test" --output-on-failure`
Expected: build fails because the ring classes are missing.

- [ ] **Step 3: Define the shared-memory layout, constants, and writer result type**

```cpp
struct SharedFrameRingHeader {
    quint32 magic = 0x524B5348; // RKSH
    quint16 version = 1;
    quint16 slotCount = 0;
    quint32 slotStride = 0;
    quint32 maxFrameBytes = 0;
    quint32 producerPid = 0;
    quint64 publishedFrames = 0;
    quint64 droppedFrames = 0;
};

struct SharedFrameSlotHeader {
    quint64 sequence = 0;
    quint64 frameId = 0;
    qint64 timestampMs = 0;
    qint32 width = 0;
    qint32 height = 0;
    qint32 pixelFormat = 0;
    quint32 payloadBytes = 0;
    quint32 flags = 0;
};

struct SharedFramePublishResult {
    quint32 slotIndex = 0;
    quint64 sequence = 0;
    quint32 payloadBytes = 0;
};
```

- [ ] **Step 4: Implement the writer with `shm_open`/`mmap` and round-robin overwrite semantics**

```cpp
SharedFramePublishResult SharedMemoryFrameRingWriter::publish(const AnalysisFramePacket &frame) {
    SharedFramePublishResult result;
    if (!mapped_ || frame.payload.size() > static_cast<int>(header_->maxFrameBytes)) {
        header_->droppedFrames++;
        return result;
    }

    const quint32 slotIndex = nextSlotIndex_++ % header_->slotCount;
    SharedFrameSlotHeader *slotHeader = slotHeaderFor(slotIndex);
    char *slotPayload = slotPayloadFor(slotIndex);
    memcpy(slotPayload, frame.payload.constData(), frame.payload.size());

    slotHeader->frameId = frame.frameId;
    slotHeader->timestampMs = frame.timestampMs;
    slotHeader->width = frame.width;
    slotHeader->height = frame.height;
    slotHeader->pixelFormat = static_cast<qint32>(frame.pixelFormat);
    slotHeader->payloadBytes = static_cast<quint32>(frame.payload.size());
    slotHeader->flags = 0;
    slotHeader->sequence += 1;

    header_->publishedFrames += 1;
    result.slotIndex = slotIndex;
    result.sequence = slotHeader->sequence;
    result.payloadBytes = slotHeader->payloadBytes;
    return result;
}
```

- [ ] **Step 5: Implement the reader with lazy map, sequence re-check, and packet reconstruction**

```cpp
bool SharedMemoryFrameReader::read(const AnalysisFrameDescriptor &descriptor,
    AnalysisFramePacket *packet, QString *error) {
    if (!ensureMapped(descriptor.cameraId, error)) {
        return false;
    }
    if (descriptor.slotIndex >= header_->slotCount) {
        if (error) *error = QStringLiteral("analysis_slot_out_of_range");
        return false;
    }

    const SharedFrameSlotHeader *slotHeader = slotHeaderFor(descriptor.slotIndex);
    const quint64 firstSequence = slotHeader->sequence;
    if (firstSequence != descriptor.sequence) {
        if (error) *error = QStringLiteral("analysis_slot_overwritten");
        return false;
    }

    packet->frameId = slotHeader->frameId;
    packet->timestampMs = slotHeader->timestampMs;
    packet->cameraId = descriptor.cameraId;
    packet->width = slotHeader->width;
    packet->height = slotHeader->height;
    packet->pixelFormat = static_cast<AnalysisPixelFormat>(slotHeader->pixelFormat);
    packet->payload = QByteArray(slotPayloadFor(descriptor.slotIndex), slotHeader->payloadBytes);

    if (slotHeader->sequence != firstSequence) {
        if (error) *error = QStringLiteral("analysis_slot_rewritten_during_read");
        return false;
    }
    return true;
}
```

- [ ] **Step 6: Link the helpers into `health-videod` and `health-falld`, then run the focused tests**

```cmake
add_executable(health-videod
    ...
    analysis/shared_memory_frame_ring.cpp
    analysis/shared_memory_frame_ring.h
)

add_executable(health-falld
    ...
    ingest/shared_memory_frame_reader.cpp
    ingest/shared_memory_frame_reader.h
)
```

Run: `cmake --build out/build-rk_app-host -j4 --target shared_memory_frame_ring_test shared_memory_frame_reader_test && ctest --test-dir out/build-rk_app-host -R "shared_memory_frame_ring_test|shared_memory_frame_reader_test" --output-on-failure`
Expected: both new tests pass.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_videod/analysis/shared_memory_frame_ring.h \
        rk_app/src/health_videod/analysis/shared_memory_frame_ring.cpp \
        rk_app/src/health_falld/ingest/shared_memory_frame_reader.h \
        rk_app/src/health_falld/ingest/shared_memory_frame_reader.cpp \
        rk_app/src/tests/video_daemon_tests/shared_memory_frame_ring_test.cpp \
        rk_app/src/tests/fall_daemon_tests/shared_memory_frame_reader_test.cpp \
        rk_app/src/tests/CMakeLists.txt \
        rk_app/src/health_videod/CMakeLists.txt \
        rk_app/src/health_falld/CMakeLists.txt
git commit -m "feat: add analysis shared memory ring primitives"
```

### Task 3: Make `health-videod` publish descriptors backed by shared memory

**Files:**
- Modify: `rk_app/src/health_videod/analysis/analysis_output_backend.h`
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h`
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/video_service_analysis_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Rewrite the backend socket test to expect a descriptor instead of a full frame payload**

```cpp
void AnalysisOutputBackendTest::publishesDescriptorToLocalSocket() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_backend_test.sock"));

    GstreamerAnalysisOutputBackend backend;
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 20;

    QString error;
    QVERIFY(backend.start(status, &error));

    QLocalSocket client;
    client.connectToServer(QStringLiteral("/tmp/rk_video_analysis_backend_test.sock"));
    QVERIFY(client.waitForConnected(2000));

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 5;
    descriptor.timestampMs = 1234;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 640;
    descriptor.height = 640;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.slotIndex = 1;
    descriptor.sequence = 4;
    descriptor.payloadBytes = 640 * 640 * 3;

    backend.publishDescriptor(descriptor);

    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() > 0 || client.waitForReadyRead(50), 2000);
    AnalysisFrameDescriptor decoded;
    QVERIFY(decodeAnalysisFrameDescriptor(client.readAll(), &decoded));
    QCOMPARE(decoded.slotIndex, descriptor.slotIndex);
    QCOMPARE(decoded.sequence, descriptor.sequence);
}
```

- [ ] **Step 2: Run the focused video-daemon tests to verify the new contract does not exist yet**

Run: `cmake --build out/build-rk_app-host -j4 --target analysis_output_backend_test gstreamer_video_pipeline_backend_test video_service_analysis_test && ctest --test-dir out/build-rk_app-host -R "analysis_output_backend_test|gstreamer_video_pipeline_backend_test|video_service_analysis_test" --output-on-failure`
Expected: build fails because `publishDescriptor()` and ring-backed publication do not exist yet.

- [ ] **Step 3: Change the output backend interface to publish descriptors**

```cpp
class AnalysisOutputBackend {
public:
    virtual ~AnalysisOutputBackend() = default;

    virtual bool start(const VideoChannelStatus &status, QString *error) = 0;
    virtual bool stop(const QString &cameraId, QString *error) = 0;
    virtual AnalysisChannelStatus statusForCamera(const QString &cameraId) const = 0;
    virtual bool acceptsFrames(const QString &cameraId) const = 0;
    virtual void publishDescriptor(const AnalysisFrameDescriptor &descriptor) = 0;
};
```

- [ ] **Step 4: Update `GstreamerAnalysisOutputBackend` to encode descriptors and report status fields**

```cpp
void GstreamerAnalysisOutputBackend::publishDescriptor(const AnalysisFrameDescriptor &descriptor) {
    if (!acceptsFrames(descriptor.cameraId)) {
        return;
    }

    const QByteArray encoded = encodeAnalysisFrameDescriptor(descriptor);
    for (int index = clients_.size() - 1; index >= 0; --index) {
        QLocalSocket *client = clients_.at(index);
        if (!client || client->state() != QLocalSocket::ConnectedState) {
            clients_.removeAt(index);
            continue;
        }
        client->write(encoded);
        client->flush();
    }

    AnalysisChannelStatus status = statuses_.value(descriptor.cameraId, defaultStatusForCamera(descriptor.cameraId));
    status.outputFormat = QStringLiteral("rgb-shm");
    status.width = descriptor.width;
    status.height = descriptor.height;
    statuses_.insert(descriptor.cameraId, status);
}
```

- [ ] **Step 5: After `stdout` frame assembly, write to the ring and publish a descriptor**

```cpp
pipeline.stdoutBuffer.append(pipeline.process->readAllStandardOutput());
while (pipeline.stdoutBuffer.size() >= pipeline.analysisFrameBytes) {
    AnalysisFramePacket frame;
    frame.frameId = pipeline.nextFrameId++;
    frame.timestampMs = QDateTime::currentMSecsSinceEpoch();
    frame.cameraId = pipeline.cameraId;
    frame.width = pipeline.analysisWidth;
    frame.height = pipeline.analysisHeight;
    frame.pixelFormat = AnalysisPixelFormat::Rgb;
    frame.payload = pipeline.stdoutBuffer.left(pipeline.analysisFrameBytes);
    pipeline.stdoutBuffer.remove(0, pipeline.analysisFrameBytes);

    const SharedFramePublishResult publish = pipeline.frameRing->publish(frame);
    if (publish.sequence == 0) {
        continue;
    }

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = frame.frameId;
    descriptor.timestampMs = frame.timestampMs;
    descriptor.cameraId = frame.cameraId;
    descriptor.width = frame.width;
    descriptor.height = frame.height;
    descriptor.pixelFormat = frame.pixelFormat;
    descriptor.slotIndex = publish.slotIndex;
    descriptor.sequence = publish.sequence;
    descriptor.payloadBytes = publish.payloadBytes;
    analysisFrameSource_->publishDescriptor(descriptor);
}
```

- [ ] **Step 6: Run the focused video-daemon tests until they pass**

Run: `cmake --build out/build-rk_app-host -j4 --target analysis_output_backend_test gstreamer_video_pipeline_backend_test video_service_analysis_test && ctest --test-dir out/build-rk_app-host -R "analysis_output_backend_test|gstreamer_video_pipeline_backend_test|video_service_analysis_test" --output-on-failure`
Expected: descriptor publication tests pass and service analysis lifecycle stays green.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_videod/analysis/analysis_output_backend.h \
        rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h \
        rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp \
        rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp \
        rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp \
        rk_app/src/tests/video_daemon_tests/video_service_analysis_test.cpp \
        rk_app/src/tests/CMakeLists.txt
git commit -m "feat: publish analysis descriptors over shm ring"
```

### Task 4: Make `health-falld` rebuild full packets from descriptors and shared memory

**Files:**
- Modify: `rk_app/src/health_falld/ingest/analysis_stream_client.h`
- Modify: `rk_app/src/health_falld/ingest/analysis_stream_client.cpp`
- Modify: `rk_app/src/health_falld/runtime/runtime_config.h`
- Modify: `rk_app/src/health_falld/runtime/runtime_config.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Rewrite the ingest client test around descriptors + shared memory**

```cpp
void AnalysisStreamClientTest::reconstructsRgbFrameFromSharedMemoryDescriptor() {
    QLocalServer server;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_rgb_test.sock"));
    QVERIFY(server.listen(QStringLiteral("/tmp/rk_video_analysis_rgb_test.sock")));

    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 4, 4 * 3 * 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket packet;
    packet.frameId = 55;
    packet.timestampMs = 999;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 4;
    packet.height = 3;
    packet.pixelFormat = AnalysisPixelFormat::Rgb;
    packet.payload = QByteArray(4 * 3 * 3, '\x33');
    const auto publish = writer.publish(packet);

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = packet.frameId;
    descriptor.timestampMs = packet.timestampMs;
    descriptor.cameraId = packet.cameraId;
    descriptor.width = packet.width;
    descriptor.height = packet.height;
    descriptor.pixelFormat = packet.pixelFormat;
    descriptor.slotIndex = publish.slotIndex;
    descriptor.sequence = publish.sequence;
    descriptor.payloadBytes = publish.payloadBytes;

    AnalysisStreamClient client(QStringLiteral("/tmp/rk_video_analysis_rgb_test.sock"));
    QSignalSpy spy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    client.start();

    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);
    socket->write(encodeAnalysisFrameDescriptor(descriptor));
    socket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 1, 2000);
    const AnalysisFramePacket decoded = qvariant_cast<AnalysisFramePacket>(spy.takeFirst().at(0));
    QCOMPARE(decoded.payload, packet.payload);
}
```

- [ ] **Step 2: Run the fall-daemon ingest tests to prove the old socket payload assumptions now fail**

Run: `cmake --build out/build-rk_app-host -j4 --target analysis_stream_client_test fall_end_to_end_status_test && ctest --test-dir out/build-rk_app-host -R "analysis_stream_client_test|fall_end_to_end_status_test" --output-on-failure`
Expected: build or runtime failure because `AnalysisStreamClient` still expects full packets from the socket.

- [ ] **Step 3: Give `AnalysisStreamClient` a reader and switch its decode path to descriptors**

```cpp
class AnalysisStreamClient : public QObject {
    Q_OBJECT
public:
    explicit AnalysisStreamClient(const QString &socketName, QObject *parent = nullptr);

private:
    void onReadyRead();

    QString socketName_;
    QLocalSocket *socket_ = nullptr;
    QTimer *reconnectTimer_ = nullptr;
    QByteArray readBuffer_;
    SharedMemoryFrameReader reader_;
    bool running_ = false;
};
```

```cpp
void AnalysisStreamClient::onReadyRead() {
    readBuffer_.append(socket_->readAll());

    AnalysisFrameDescriptor descriptor;
    AnalysisFramePacket latestPacket;
    bool hasPacket = false;
    while (takeFirstAnalysisFrameDescriptor(&readBuffer_, &descriptor)) {
        QString error;
        AnalysisFramePacket packet;
        if (reader_.read(descriptor, &packet, &error)) {
            latestPacket = packet;
            hasPacket = true;
        }
    }

    if (hasPacket) {
        emit frameReceived(latestPacket);
    }
}
```

- [ ] **Step 4: Add runtime config for an optional shm name override and use it in the reader**

```cpp
struct FallRuntimeConfig {
    QString cameraId = QStringLiteral("front_cam");
    QString socketName = QStringLiteral("rk_fall.sock");
    QString analysisSocketPath = QStringLiteral("/tmp/rk_video_analysis.sock");
    QString analysisSharedMemoryName;
    ...
};
```

```cpp
const QString analysisSharedMemoryName = qEnvironmentVariable("RK_VIDEO_ANALYSIS_SHM_NAME");
if (!analysisSharedMemoryName.isEmpty()) {
    config.analysisSharedMemoryName = analysisSharedMemoryName;
}
```

- [ ] **Step 5: Update the end-to-end status test helper to publish descriptors through shared memory**

```cpp
void streamRgbAnalysisFrames(QLocalSocket *analysisSocket,
    SharedMemoryFrameRingWriter *writer, quint64 firstFrameId, quint64 lastFrameId, int waitMs = 5) {
    QVERIFY(analysisSocket != nullptr);
    QVERIFY(writer != nullptr);
    for (quint64 frameId = firstFrameId; frameId <= lastFrameId; ++frameId) {
        AnalysisFramePacket packet;
        packet.frameId = frameId;
        packet.cameraId = QStringLiteral("front_cam");
        packet.width = 640;
        packet.height = 640;
        packet.pixelFormat = AnalysisPixelFormat::Rgb;
        packet.payload = QByteArray(640 * 640 * 3, '\x20');

        const auto publish = writer->publish(packet);
        AnalysisFrameDescriptor descriptor;
        descriptor.frameId = packet.frameId;
        descriptor.timestampMs = packet.timestampMs;
        descriptor.cameraId = packet.cameraId;
        descriptor.width = packet.width;
        descriptor.height = packet.height;
        descriptor.pixelFormat = packet.pixelFormat;
        descriptor.slotIndex = publish.slotIndex;
        descriptor.sequence = publish.sequence;
        descriptor.payloadBytes = publish.payloadBytes;
        analysisSocket->write(encodeAnalysisFrameDescriptor(descriptor));
        analysisSocket->flush();
        QTest::qWait(waitMs);
    }
}
```

- [ ] **Step 6: Run the focused fall-daemon tests until they pass**

Run: `cmake --build out/build-rk_app-host -j4 --target analysis_stream_client_test fall_end_to_end_status_test && ctest --test-dir out/build-rk_app-host -R "analysis_stream_client_test|fall_end_to_end_status_test" --output-on-failure`
Expected: ingest client tests pass and end-to-end status tests still see valid frames and classifications.

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_falld/ingest/analysis_stream_client.h \
        rk_app/src/health_falld/ingest/analysis_stream_client.cpp \
        rk_app/src/health_falld/runtime/runtime_config.h \
        rk_app/src/health_falld/runtime/runtime_config.cpp \
        rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp \
        rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp \
        rk_app/src/tests/CMakeLists.txt
git commit -m "feat: read analysis frames from shared memory"
```

### Task 5: Add transport-level counters and latency markers for baseline comparison

**Files:**
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`
- Modify: `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h`
- Modify: `rk_app/src/health_falld/ingest/analysis_stream_client.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp`
- Modify: `rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp`
- Modify: `deploy/tests/measure_rk3588_test_mode_latency.py`
- Modify: `deploy/tests/measure_rk3588_test_mode_latency_test.py`

- [ ] **Step 1: Write the failing measurement-script unit test for transport latency comparison**

```python
from measure_rk3588_test_mode_latency import compute_metrics, compare_runs


def test_compare_runs_reports_transport_improvement():
    baseline = {
        "analysis_publish_ts_ms": 1000,
        "analysis_ingest_ts_ms": 1040,
        "producer_cpu_pct": 22.0,
        "consumer_cpu_pct": 18.0,
        "producer_dropped_frames": 5,
    }
    candidate = {
        "analysis_publish_ts_ms": 1000,
        "analysis_ingest_ts_ms": 1018,
        "producer_cpu_pct": 17.0,
        "consumer_cpu_pct": 15.0,
        "producer_dropped_frames": 1,
    }

    comparison = compare_runs(baseline, candidate)
    assert comparison["transport_latency_delta_ms"] == -22
    assert comparison["producer_cpu_delta_pct"] == -5.0
    assert comparison["producer_drop_delta"] == -4
```

- [ ] **Step 2: Run the script unit test to prove the comparison helper is missing**

Run: `python3 -m unittest deploy/tests/measure_rk3588_test_mode_latency_test.py`
Expected: failure because `compare_runs` and transport metrics do not exist yet.

- [ ] **Step 3: Emit transport publish and ingest markers in the runtime**

```cpp
// in health-videod after descriptor publication
LatencyMarkerWriter marker(qEnvironmentVariable("RK_VIDEO_LATENCY_MARKER_PATH"));
marker.writeEvent(QStringLiteral("analysis_descriptor_published"), descriptor.timestampMs,
    QJsonObject {
        {QStringLiteral("camera_id"), descriptor.cameraId},
        {QStringLiteral("frame_id"), static_cast<qint64>(descriptor.frameId)},
        {QStringLiteral("slot_index"), static_cast<int>(descriptor.slotIndex)},
        {QStringLiteral("sequence"), static_cast<qint64>(descriptor.sequence)},
        {QStringLiteral("dropped_frames"), static_cast<qint64>(statusForCamera(descriptor.cameraId).droppedFrames)},
    });
```

```cpp
// in health-falld after successful shm read
LatencyMarkerWriter marker(qEnvironmentVariable("RK_FALL_LATENCY_MARKER_PATH"));
marker.writeEvent(QStringLiteral("analysis_descriptor_ingested"), QDateTime::currentMSecsSinceEpoch(),
    QJsonObject {
        {QStringLiteral("camera_id"), packet.cameraId},
        {QStringLiteral("frame_id"), static_cast<qint64>(packet.frameId)},
    });
```

- [ ] **Step 4: Extend the Python harness to compute transport latency and before/after deltas**

```python
def compute_metrics(video_events, fall_events):
    playback = next(event for event in video_events if event["event"] == "playback_started")
    published = next(event for event in video_events if event["event"] == "analysis_descriptor_published")
    ingested = next(event for event in fall_events if event["event"] == "analysis_descriptor_ingested")
    first_frame = next(event for event in fall_events if event["event"] == "first_analysis_frame")
    first_classification = next(event for event in fall_events if event["event"] == "first_classification")
    return {
        "playback_start_ts_ms": playback["ts_ms"],
        "analysis_publish_ts_ms": published["ts_ms"],
        "analysis_ingest_ts_ms": ingested["ts_ms"],
        "transport_latency_ms": ingested["ts_ms"] - published["ts_ms"],
        "analysis_ingress_latency_ms": first_frame["ts_ms"] - playback["ts_ms"],
        "startup_classification_latency_ms": first_classification["ts_ms"] - playback["ts_ms"],
        "producer_dropped_frames": published.get("dropped_frames", 0),
    }


def compare_runs(baseline, candidate):
    return {
        "transport_latency_delta_ms": candidate["transport_latency_ms"] - baseline["transport_latency_ms"],
        "producer_drop_delta": candidate["producer_dropped_frames"] - baseline["producer_dropped_frames"],
        "producer_cpu_delta_pct": candidate["producer_cpu_pct"] - baseline["producer_cpu_pct"],
        "consumer_cpu_delta_pct": candidate["consumer_cpu_pct"] - baseline["consumer_cpu_pct"],
    }
```

- [ ] **Step 5: Re-run the marker and script tests until they pass**

Run: `cmake --build out/build-rk_app-host -j4 --target analysis_output_backend_test analysis_stream_client_test && ctest --test-dir out/build-rk_app-host -R "analysis_output_backend_test|analysis_stream_client_test" --output-on-failure && python3 -m unittest deploy/tests/measure_rk3588_test_mode_latency_test.py`
Expected: runtime tests still pass and the Python measurement tests pass with the new transport metrics.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.h \
        rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp \
        rk_app/src/health_falld/ingest/analysis_stream_client.cpp \
        rk_app/src/tests/video_daemon_tests/analysis_output_backend_test.cpp \
        rk_app/src/tests/fall_daemon_tests/analysis_stream_client_test.cpp \
        deploy/tests/measure_rk3588_test_mode_latency.py \
        deploy/tests/measure_rk3588_test_mode_latency_test.py
git commit -m "feat: add shm transport latency metrics"
```

### Task 6: Run the full verification set, compare against `main`, and record the result

**Files:**
- Modify: `docs/testing/2026-04-23-analysis-shm-transport-baseline.md`
- Verify: `out/build-rk_app-host`
- Verify: RK3588 bundle output and benchmark environment

- [ ] **Step 1: Run the full focused host test suite for the transport change**

Run: `cmake --build out/build-rk_app-host -j4 --target analysis_frame_descriptor_protocol_test shared_memory_frame_ring_test shared_memory_frame_reader_test analysis_output_backend_test gstreamer_video_pipeline_backend_test video_service_analysis_test analysis_stream_client_test fall_end_to_end_status_test && ctest --test-dir out/build-rk_app-host -R "analysis_frame_descriptor_protocol_test|shared_memory_frame_ring_test|shared_memory_frame_reader_test|analysis_output_backend_test|gstreamer_video_pipeline_backend_test|video_service_analysis_test|analysis_stream_client_test|fall_end_to_end_status_test" --output-on-failure`
Expected: all transport-focused host tests pass.

- [ ] **Step 2: Build the RK3588 bundle for the candidate implementation**

Run: `bash deploy/scripts/build_rk3588_bundle.sh`
Expected: bundle build finishes successfully and the RK3588 deployable artifacts are refreshed.

- [ ] **Step 3: Measure the current `main` branch as the baseline on hardware**

```bash
python3 deploy/tests/measure_rk3588_test_mode_latency.py \
  --host <rk3588-host> \
  --password '<password>' \
  --bundle-dir <baseline-main-bundle> \
  --video-file /home/elf/Videos/video.mp4 > /tmp/analysis-shm-baseline.json
```

Expected: JSON contains baseline values for `transport_latency_ms`, `analysis_ingress_latency_ms`, `startup_classification_latency_ms`, CPU, and dropped frames.

- [ ] **Step 4: Measure the shared-memory implementation on the same hardware**

```bash
python3 deploy/tests/measure_rk3588_test_mode_latency.py \
  --host <rk3588-host> \
  --password '<password>' \
  --bundle-dir <candidate-shm-bundle> \
  --video-file /home/elf/Videos/video.mp4 > /tmp/analysis-shm-candidate.json
```

Expected: JSON contains the same metric set for the shared-memory build.

- [ ] **Step 5: Record the comparison in the checked-in results note**

```md
# Analysis SHM Transport Baseline

- Baseline branch: `main`
- Baseline commit: `<main-commit>`
- Candidate commit: `<candidate-commit>`
- Hardware: `RK3588 <board name>`
- Video: `/home/elf/Videos/video.mp4`

## Commands
- `python3 deploy/tests/measure_rk3588_test_mode_latency.py ...`

## Raw Metrics
- baseline transport latency: `<value>` ms
- candidate transport latency: `<value>` ms
- baseline producer CPU: `<value>` %
- candidate producer CPU: `<value>` %
- baseline consumer CPU: `<value>` %
- candidate consumer CPU: `<value>` %
- baseline dropped frames: `<value>`
- candidate dropped frames: `<value>`

## Conclusion
- The shared-memory transport is `<better / not better>` than `main` because `<brief technical reason>`.
```

- [ ] **Step 6: Commit**

```bash
git add docs/testing/2026-04-23-analysis-shm-transport-baseline.md
git commit -m "docs: record analysis shm transport baseline"
```

## Self-Review

- Spec coverage: the plan covers descriptor protocol, shared-memory ring writer/reader, producer integration, consumer integration, error-tolerant frame loss semantics, and the required performance comparison against the current `main` branch.
- Placeholder scan: no `TODO`/`TBD` markers remain; every task names exact files, test targets, commands, and representative code.
- Type consistency: the plan consistently uses `AnalysisFrameDescriptor`, `SharedMemoryFrameRingWriter`, `SharedMemoryFrameReader`, `publishDescriptor()`, and `frameReceived(AnalysisFramePacket)`.

# GStreamer Pipeline Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `health-videod`'s GStreamer backend into focused pipeline components without changing preview, recording, snapshot, analysis transport, or runtime-config behavior.

**Architecture:** Keep `GstreamerVideoPipelineBackend` as the orchestration entry point, but extract command building, preview stream parsing, DMA allocation, analysis frame publishing, and external process lifecycle into focused collaborators. Preserve the existing external `gst-launch` path and the optional in-process GStreamer path, then introduce runner selection only after concrete component boundaries are stable.

**Tech Stack:** C++17, Qt 5/6 Core/Network/Test, existing `health-videod` analysis transport classes, optional GStreamer app/video/allocators modules.

---

## File Structure

### Files to Create

- `rk_app/src/health_videod/pipeline/gst_command_builder.h`
  - focused preview/recording/snapshot command construction
- `rk_app/src/health_videod/pipeline/gst_command_builder.cpp`
  - shell quoting, preview URL helpers, analysis tap fragment building
- `rk_app/src/health_videod/pipeline/preview_stream_reader.h`
  - preview URL parsing and single-frame read API
- `rk_app/src/health_videod/pipeline/preview_stream_reader.cpp`
  - TCP multipart MJPEG read loop
- `rk_app/src/health_videod/pipeline/multipart_jpeg_parser.h`
  - incremental multipart parser interface
- `rk_app/src/health_videod/pipeline/multipart_jpeg_parser.cpp`
  - extracted multipart parsing logic
- `rk_app/src/health_videod/pipeline/dma_buffer_allocator.h`
  - memfd/dma-heap allocation API
- `rk_app/src/health_videod/pipeline/dma_buffer_allocator.cpp`
  - allocation and payload-write helpers
- `rk_app/src/health_videod/pipeline/pipeline_session.h`
  - per-camera mutable session state
- `rk_app/src/health_videod/pipeline/analysis_frame_publisher.h`
  - byte-frame and DMA-frame publish API
- `rk_app/src/health_videod/pipeline/analysis_frame_publisher.cpp`
  - RGA/CPU conversion, descriptor publication, latency/logging
- `rk_app/src/health_videod/pipeline/gst_process_runner.h`
  - external process lifecycle API
- `rk_app/src/health_videod/pipeline/gst_process_runner.cpp`
  - `QProcess` startup, exit, and stop policy
- `rk_app/src/health_videod/pipeline/video_pipeline_runner.h`
  - narrow preview runner interface for external vs in-process execution
- `rk_app/src/health_videod/pipeline/external_gstreamer_runner.h`
  - adapter from backend orchestration to `GstProcessRunner`
- `rk_app/src/health_videod/pipeline/external_gstreamer_runner.cpp`
  - external preview runner implementation
- `rk_app/src/health_videod/pipeline/pipeline_runner_factory.h`
  - runtime selection for preview runner implementation
- `rk_app/src/health_videod/pipeline/pipeline_runner_factory.cpp`
  - select external vs in-process runner
- `rk_app/src/health_videod/pipeline/inprocess_launch_description_builder.h`
  - `gst_parse_launch()` string builder
- `rk_app/src/health_videod/pipeline/inprocess_launch_description_builder.cpp`
  - launch description assembly
- `rk_app/src/health_videod/pipeline/gst_bus_monitor.h`
  - bus polling callback helper
- `rk_app/src/health_videod/pipeline/gst_bus_monitor.cpp`
  - EOS/error extraction
- `rk_app/src/health_videod/pipeline/gst_appsink_frame_dispatcher.h`
  - appsink sample handling and queued callback dispatch
- `rk_app/src/health_videod/pipeline/gst_appsink_frame_dispatcher.cpp`
  - DMA-sample detection and fallback byte mapping
- `rk_app/src/tests/video_daemon_tests/gstreamer_pipeline_components_test.cpp`
  - unit coverage for extracted command, parser, reader, and publisher components
- `rk_app/src/tests/video_daemon_tests/gst_process_runner_test.cpp`
  - unit coverage for startup/stop policy

### Files to Modify

- `rk_app/src/health_videod/CMakeLists.txt`
  - add new pipeline component sources to `health-videod`
- `rk_app/src/tests/CMakeLists.txt`
  - add new test targets and include new component sources
- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
  - reduce private helpers, remove `friend`, replace `ActivePipeline` with session-aware orchestration
- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
  - backend orchestration only
- `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.h`
  - shrink to lifecycle + callback registration
- `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.cpp`
  - delegate launch building, bus polling, and appsink handling
- `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`
  - backend orchestration tests only; remove private-state setup patterns

### Files to Keep Unchanged

- `rk_app/src/health_videod/analysis/shared_memory_frame_ring.*`
  - publisher uses existing implementation
- `rk_app/src/health_videod/analysis/rga_frame_converter.*`
  - publisher keeps existing conversion dependency
- `rk_app/src/health_videod/debug/video_runtime_log_stats.*`
  - publisher reuses existing runtime summary logic

---

### Task 1: Extract Multipart Preview Parsing and Preview Stream Reader

**Files:**
- Create: `rk_app/src/health_videod/pipeline/multipart_jpeg_parser.h`
- Create: `rk_app/src/health_videod/pipeline/multipart_jpeg_parser.cpp`
- Create: `rk_app/src/health_videod/pipeline/preview_stream_reader.h`
- Create: `rk_app/src/health_videod/pipeline/preview_stream_reader.cpp`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/health_videod/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Create: `rk_app/src/tests/video_daemon_tests/gstreamer_pipeline_components_test.cpp`

- [ ] **Step 1: Write failing parser and preview-reader tests**

Add tests that cover:

```cpp
void extractsSingleJpegFromMultipartPayload();
void ignoresNonJpegMultipartChunk();
void rejectsInvalidPreviewUrl();
void readsJpegFrameFromTcpMultipartPreview();
```

Use the existing `buildMultipartFrame()` and `sampleJpegBytes()` helpers from `gstreamer_video_pipeline_backend_test.cpp` as the starting pattern for the new component test file.

- [ ] **Step 2: Run the new component test target and verify failure**

Run:

```bash
cmake --build out/build-rk_app-host --target gstreamer_pipeline_components_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R gstreamer_pipeline_components_test --output-on-failure
```

Expected:

- build fails because the new component files and target do not exist yet, or
- tests fail because `MultipartJpegParser` and `PreviewStreamReader` are not implemented

- [ ] **Step 3: Implement the minimal parser and reader**

Create these interfaces:

```cpp
class MultipartJpegParser {
public:
    bool takeFrame(QByteArray *streamBuffer, const QByteArray &boundaryMarker, QByteArray *jpegBytes) const;
};

class PreviewStreamReader {
public:
    struct PreviewStreamConfig {
        QString host;
        quint16 port = 0;
        QString boundary;
    };

    bool parsePreviewUrl(const QString &previewUrl, PreviewStreamConfig *config, QString *error) const;
    bool readJpegFrame(const QString &previewUrl, QByteArray *jpegBytes, QString *error) const;
};
```

Move the current URL parsing and TCP read loop out of `GstreamerVideoPipelineBackend` into these classes without changing behavior.

- [ ] **Step 4: Rewire backend snapshot code to the new reader and run tests**

Backend change target:

```cpp
bool GstreamerVideoPipelineBackend::captureSnapshot(...) {
    if (!status.previewUrl.isEmpty()) {
        return previewStreamReader_.readJpegFrame(status.previewUrl, &jpegBytes, error);
    }
    ...
}
```

Run:

```bash
cmake --build out/build-rk_app-host --target gstreamer_pipeline_components_test gstreamer_video_pipeline_backend_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R "gstreamer_pipeline_components_test|gstreamer_video_pipeline_backend_test" --output-on-failure
```

Expected:

- parser/reader tests pass
- existing snapshot-from-preview backend test still passes

- [ ] **Step 5: Commit**

```bash
git add rk_app/src/health_videod/pipeline/multipart_jpeg_parser.* \
        rk_app/src/health_videod/pipeline/preview_stream_reader.* \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp \
        rk_app/src/health_videod/CMakeLists.txt \
        rk_app/src/tests/CMakeLists.txt \
        rk_app/src/tests/video_daemon_tests/gstreamer_pipeline_components_test.cpp
git commit -m "refactor: extract preview stream reader"
```

### Task 2: Extract DMA Buffer Allocator and GStreamer Command Builder

**Files:**
- Create: `rk_app/src/health_videod/pipeline/dma_buffer_allocator.h`
- Create: `rk_app/src/health_videod/pipeline/dma_buffer_allocator.cpp`
- Create: `rk_app/src/health_videod/pipeline/gst_command_builder.h`
- Create: `rk_app/src/health_videod/pipeline/gst_command_builder.cpp`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/health_videod/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_pipeline_components_test.cpp`

- [ ] **Step 1: Add failing command-builder and allocator tests**

Add tests that assert:

```cpp
void buildsExternalPreviewCommandWithoutAnalysisTap();
void buildsExternalPreviewCommandWithRgaAnalysisTap();
void buildsPreviewStreamRecordingCommand();
void quotesShellArgumentsWithSingleQuotes();
void allocatesMemfdBufferWhenHeapPathIsMemfd();
```

Keep the assertions aligned with the current backend tests, for example checking `mppjpegenc`, `multipartmux`, `fdsink fd=1`, and `framerate=15/1`.

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
cmake --build out/build-rk_app-host --target gstreamer_pipeline_components_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R gstreamer_pipeline_components_test --output-on-failure
```

Expected:

- component tests fail because `GstCommandBuilder` and `DmaBufferAllocator` do not exist

- [ ] **Step 3: Implement concrete helper classes**

Create these APIs:

```cpp
class GstCommandBuilder {
public:
    explicit GstCommandBuilder(const AppRuntimeConfig &runtimeConfig);

    QString previewUrlForCamera(const QString &cameraId) const;
    QString previewBoundaryForCamera(const QString &cameraId) const;
    quint16 previewPortForCamera(const QString &cameraId) const;
    QString buildPreviewCommand(const VideoChannelStatus &status, bool analysisTapEnabled) const;
    QString buildRecordingCommand(const VideoChannelStatus &status, const QString &outputPath, bool analysisTapEnabled) const;
    QString buildSnapshotCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    QString buildPreviewStreamRecordingCommand(const QString &previewUrl, const QString &outputPath, QString *error) const;
};

class DmaBufferAllocator {
public:
    int allocate(const QString &heapPath, int bytes, QString *error) const;
    bool writePayload(int fd, const QByteArray &payload, QString *error) const;
};
```

Move the current command-generation code and memfd/dma-heap helpers into these classes without changing command strings.

- [ ] **Step 4: Replace backend-local helpers and rerun focused tests**

Backend target shape:

```cpp
class GstreamerVideoPipelineBackend : public VideoPipelineBackend {
    ...
    GstCommandBuilder commandBuilder_;
    PreviewStreamReader previewStreamReader_;
    DmaBufferAllocator dmaBufferAllocator_;
};
```

Run:

```bash
cmake --build out/build-rk_app-host --target gstreamer_pipeline_components_test gstreamer_video_pipeline_backend_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R "gstreamer_pipeline_components_test|gstreamer_video_pipeline_backend_test" --output-on-failure
```

Expected:

- command-builder tests pass
- existing backend command behavior tests still pass

- [ ] **Step 5: Commit**

```bash
git add rk_app/src/health_videod/pipeline/dma_buffer_allocator.* \
        rk_app/src/health_videod/pipeline/gst_command_builder.* \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp \
        rk_app/src/health_videod/CMakeLists.txt \
        rk_app/src/tests/CMakeLists.txt \
        rk_app/src/tests/video_daemon_tests/gstreamer_pipeline_components_test.cpp
git commit -m "refactor: extract gst command and dma helpers"
```

### Task 3: Introduce Pipeline Session and Analysis Frame Publisher

**Files:**
- Create: `rk_app/src/health_videod/pipeline/pipeline_session.h`
- Create: `rk_app/src/health_videod/pipeline/analysis_frame_publisher.h`
- Create: `rk_app/src/health_videod/pipeline/analysis_frame_publisher.cpp`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/health_videod/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_pipeline_components_test.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`

- [ ] **Step 1: Add failing publisher tests for byte and DMA paths**

Add tests that cover:

```cpp
void publishesRgbFrameViaSharedMemoryDescriptor();
void publishesRgaDmaOutputWhenConverterSucceeds();
void rejectsRgaDmaInputOutputWhenConverterFails();
void incrementsFrameIdOnlyOnSuccessfulPublish();
```

Move the current private-state-driven assertions from `gstreamer_video_pipeline_backend_test.cpp` into direct component tests that instantiate a `PipelineSession` and `AnalysisFramePublisher`.

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
cmake --build out/build-rk_app-host --target gstreamer_pipeline_components_test gstreamer_video_pipeline_backend_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R "gstreamer_pipeline_components_test|gstreamer_video_pipeline_backend_test" --output-on-failure
```

Expected:

- component tests fail because `PipelineSession` and `AnalysisFramePublisher` do not exist

- [ ] **Step 3: Implement session state and publisher**

Use a focused session type:

```cpp
enum class AnalysisConvertBackend {
    GstreamerCpu,
    Rga,
};

struct PipelineSession {
    QString cameraId;
    QString previewUrl;
    bool recording = false;
    bool testInput = false;
    AnalysisFrameInputFormat analysisInputFormat = AnalysisFrameInputFormat::Nv12;
    AnalysisConvertBackend analysisConvertBackend = AnalysisConvertBackend::GstreamerCpu;
    int analysisInputWidth = 0;
    int analysisInputHeight = 0;
    int analysisInputFrameBytes = 0;
    int analysisOutputWidth = 0;
    int analysisOutputHeight = 0;
    int analysisOutputFrameBytes = 0;
    quint64 nextFrameId = 1;
    QByteArray stdoutBuffer;
    SharedMemoryFrameRingWriter *frameRing = nullptr;
    VideoRuntimeLogStats logStats;
};
```

Publisher API target:

```cpp
class AnalysisFramePublisher {
public:
    explicit AnalysisFramePublisher(const AppRuntimeConfig &runtimeConfig);

    void setFrameSource(AnalysisFrameSource *source);
    void setFrameConverter(AnalysisFrameConverter *converter);

    void publishBytes(PipelineSession &session, const QByteArray &inputFrame);
    bool publishDma(PipelineSession &session, const AnalysisDmaBuffer &inputFrame);
};
```

Move all current conversion, descriptor publication, runtime summary, and latency marker logic out of the backend into the publisher.

- [ ] **Step 4: Rewire backend to use session + publisher and remove `friend` dependence**

Backend target shape:

```cpp
QHash<QString, PipelineSession> sessions_;
AnalysisFramePublisher framePublisher_;
```

Replace:

- `processAnalysisStdout()` body to accumulate bytes and call `framePublisher_.publishBytes(...)`
- direct DMA handling to call `framePublisher_.publishDma(...)`
- test-only direct `backend.pipelines_` mutation patterns with component tests

Run:

```bash
cmake --build out/build-rk_app-host --target gstreamer_pipeline_components_test gstreamer_video_pipeline_backend_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R "gstreamer_pipeline_components_test|gstreamer_video_pipeline_backend_test" --output-on-failure
```

Expected:

- component tests cover old frame-processing logic directly
- backend test no longer needs `friend class GstreamerVideoPipelineBackendTest`

- [ ] **Step 5: Commit**

```bash
git add rk_app/src/health_videod/pipeline/pipeline_session.h \
        rk_app/src/health_videod/pipeline/analysis_frame_publisher.* \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp \
        rk_app/src/health_videod/CMakeLists.txt \
        rk_app/src/tests/CMakeLists.txt \
        rk_app/src/tests/video_daemon_tests/gstreamer_pipeline_components_test.cpp \
        rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp
git commit -m "refactor: extract analysis frame publisher"
```

### Task 4: Extract External Process Lifecycle and Narrow Preview Runner Boundary

**Files:**
- Create: `rk_app/src/health_videod/pipeline/gst_process_runner.h`
- Create: `rk_app/src/health_videod/pipeline/gst_process_runner.cpp`
- Create: `rk_app/src/health_videod/pipeline/video_pipeline_runner.h`
- Create: `rk_app/src/health_videod/pipeline/external_gstreamer_runner.h`
- Create: `rk_app/src/health_videod/pipeline/external_gstreamer_runner.cpp`
- Create: `rk_app/src/health_videod/pipeline/pipeline_runner_factory.h`
- Create: `rk_app/src/health_videod/pipeline/pipeline_runner_factory.cpp`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`
- Modify: `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- Modify: `rk_app/src/health_videod/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Create: `rk_app/src/tests/video_daemon_tests/gst_process_runner_test.cpp`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`

- [ ] **Step 1: Add failing lifecycle tests**

Create tests that assert:

```cpp
void rejectsProcessThatExitsDuringStartupProbe();
void stopsProcessWithSigintThenKillFallback();
void reportsPreviewPipelineFailureToCallback();
```

Use the existing fake shell-launcher pattern from `gstreamer_video_pipeline_backend_test.cpp` to keep process behavior deterministic.

- [ ] **Step 2: Run lifecycle tests and confirm failure**

Run:

```bash
cmake --build out/build-rk_app-host --target gst_process_runner_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R gst_process_runner_test --output-on-failure
```

Expected:

- build or tests fail because `GstProcessRunner` does not exist

- [ ] **Step 3: Implement process runner and external runner**

Use a narrow callback-driven API:

```cpp
class GstProcessRunner : public QObject {
    Q_OBJECT
public:
    struct Callbacks {
        std::function<void()> onStdoutReady;
        std::function<void(int, QProcess::ExitStatus)> onFinished;
    };

    bool start(const QString &command, QProcess::ProcessChannelMode mode, const Callbacks &callbacks, QString *error);
    bool stop(QString *error);
    QProcess *process() const;
};
```

Add a runner interface only at the preview execution boundary:

```cpp
class VideoPipelineRunner {
public:
    virtual ~VideoPipelineRunner() = default;
    virtual bool startPreview(PipelineSession &session, QString *error) = 0;
    virtual bool stopPreview(PipelineSession &session, QString *error) = 0;
};
```

Then implement `ExternalGstreamerRunner` with `GstProcessRunner` and existing command-builder/publisher dependencies.

- [ ] **Step 4: Rewire backend orchestration and verify compatibility**

Backend target shape:

```cpp
std::unique_ptr<VideoPipelineRunner> runner =
    PipelineRunnerFactory(runtimeConfig_, ...).createPreviewRunner(status);
```

Keep recording-process lifecycle on the existing backend path in this task by design; this task only moves preview process supervision behind the runner boundary.

Run:

```bash
cmake --build out/build-rk_app-host --target gst_process_runner_test gstreamer_video_pipeline_backend_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R "gst_process_runner_test|gstreamer_video_pipeline_backend_test" --output-on-failure
```

Expected:

- lifecycle tests pass
- existing preview-start/stop backend tests still pass

- [ ] **Step 5: Commit**

```bash
git add rk_app/src/health_videod/pipeline/gst_process_runner.* \
        rk_app/src/health_videod/pipeline/video_pipeline_runner.h \
        rk_app/src/health_videod/pipeline/external_gstreamer_runner.* \
        rk_app/src/health_videod/pipeline/pipeline_runner_factory.* \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h \
        rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp \
        rk_app/src/health_videod/CMakeLists.txt \
        rk_app/src/tests/CMakeLists.txt \
        rk_app/src/tests/video_daemon_tests/gst_process_runner_test.cpp \
        rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp
git commit -m "refactor: extract gst process runner"
```

### Task 5: Split In-Process GStreamer Internals Behind Focused Collaborators

**Files:**
- Create: `rk_app/src/health_videod/pipeline/inprocess_launch_description_builder.h`
- Create: `rk_app/src/health_videod/pipeline/inprocess_launch_description_builder.cpp`
- Create: `rk_app/src/health_videod/pipeline/gst_bus_monitor.h`
- Create: `rk_app/src/health_videod/pipeline/gst_bus_monitor.cpp`
- Create: `rk_app/src/health_videod/pipeline/gst_appsink_frame_dispatcher.h`
- Create: `rk_app/src/health_videod/pipeline/gst_appsink_frame_dispatcher.cpp`
- Modify: `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.h`
- Modify: `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.cpp`
- Modify: `rk_app/src/health_videod/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`
- Modify: `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`

- [ ] **Step 1: Add failing tests for the extracted in-process helpers**

Add focused tests inside `gstreamer_pipeline_components_test.cpp` for:

```cpp
void buildsInprocessLaunchDescriptionWithRgbAnalysisBranch();
void buildsInprocessLaunchDescriptionWithDmabufIoMode();
```

Keep runtime backend selection behavior in `gstreamer_video_pipeline_backend_test.cpp`.

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
cmake --build out/build-rk_app-host --target gstreamer_pipeline_components_test gstreamer_video_pipeline_backend_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R "gstreamer_pipeline_components_test|gstreamer_video_pipeline_backend_test" --output-on-failure
```

Expected:

- component tests fail because the new in-process helper classes do not exist

- [ ] **Step 3: Implement launch builder, bus monitor, and appsink dispatcher**

Use these seam APIs:

```cpp
class InprocessLaunchDescriptionBuilder {
public:
    QString build(const InprocessGstreamerPipeline::Config &config) const;
};

class GstBusMonitor {
public:
    void poll(GstElement *pipeline, const std::function<void(const QString &)> &reportError) const;
};

class GstAppSinkFrameDispatcher : public QObject {
    Q_OBJECT
public:
    void setFrameCallback(InprocessGstreamerPipeline::FrameCallback callback);
    void setDmaFrameCallback(InprocessGstreamerPipeline::DmaFrameCallback callback);
    void dispatchSample(GstSample *sample);
};
```

Move the current `buildLaunchDescription()`, `pollBus()`, and `dispatchDmaFrame()/dispatchFrame()` logic into these collaborators.

- [ ] **Step 4: Slim `InprocessGstreamerPipeline` and rerun GStreamer-gated tests**

Keep `InprocessGstreamerPipeline` responsible only for:

- callback registration
- `start()`
- `stop()`
- owning `GstElement *pipeline_`, `GstElement *analysisSink_`, and the helper instances

Run:

```bash
cmake --build out/build-rk_app-host --target gstreamer_pipeline_components_test gstreamer_video_pipeline_backend_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R "gstreamer_pipeline_components_test|gstreamer_video_pipeline_backend_test" --output-on-failure
```

Expected:

- host tests pass without changing unsupported-build behavior
- when `RKAPP_ENABLE_INPROCESS_GSTREAMER=ON`, launch-string coverage and existing backend gating tests still pass

- [ ] **Step 5: Commit**

```bash
git add rk_app/src/health_videod/pipeline/inprocess_launch_description_builder.* \
        rk_app/src/health_videod/pipeline/gst_bus_monitor.* \
        rk_app/src/health_videod/pipeline/gst_appsink_frame_dispatcher.* \
        rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.h \
        rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.cpp \
        rk_app/src/health_videod/CMakeLists.txt \
        rk_app/src/tests/CMakeLists.txt \
        rk_app/src/tests/video_daemon_tests/gstreamer_pipeline_components_test.cpp \
        rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp
git commit -m "refactor: split inprocess gstreamer helpers"
```

### Task 6: Full Verification and Cleanup

**Files:**
- Modify only if verification reveals regressions.

- [ ] **Step 1: Run focused unit tests for all extracted components**

Run:

```bash
cmake --build out/build-rk_app-host --target \
    gstreamer_pipeline_components_test \
    gst_process_runner_test \
    gstreamer_video_pipeline_backend_test -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build-rk_app-host -R \
    "gstreamer_pipeline_components_test|gst_process_runner_test|gstreamer_video_pipeline_backend_test" \
    --output-on-failure
```

Expected:

- all pipeline component tests pass

- [ ] **Step 2: Run the full host suite**

Run:

```bash
ctest --test-dir out/build-rk_app-host --output-on-failure
```

Expected:

- full host test suite passes

- [ ] **Step 3: Build the RK3588 bundle**

Run:

```bash
bash deploy/scripts/build_rk3588_bundle.sh
```

Expected:

- ARM64 bundle builds successfully with no new link errors in `health-videod`

- [ ] **Step 4: Perform board smoke checks if hardware is available**

Run on target:

```bash
systemctl restart health-videod
journalctl -u health-videod -n 200 --no-pager
```

If in-process GStreamer is enabled for the build, also run:

```bash
RK_VIDEO_PIPELINE_BACKEND=inproc_gst /usr/local/bin/health-videod
```

Verify:

- preview start/stop still works
- recording start/stop still works
- snapshot from preview stream still works
- `video_perf` summaries still appear
- analysis descriptor transport still reaches `health-falld`

- [ ] **Step 5: Final cleanup commit**

```bash
git add rk_app/src/health_videod/CMakeLists.txt \
        rk_app/src/tests/CMakeLists.txt \
        rk_app/src/health_videod/pipeline \
        rk_app/src/tests/video_daemon_tests
git commit -m "refactor: modularize gstreamer video pipeline backend"
```

## Self-Review

- Spec coverage:
  - command extraction is covered by Task 2
  - IO helper extraction is covered by Tasks 1 and 2
  - analysis publisher + session split is covered by Task 3
  - process lifecycle split and runner factory boundary are covered by Task 4
  - `InprocessGstreamerPipeline` internal cleanup is covered by Task 5
  - verification and compatibility checks are covered by Task 6
- Placeholder scan:
  - no `TODO`, `TBD`, or deferred implementation markers remain
  - each task includes exact file paths and explicit verification commands
- Type consistency:
  - plan uses `PipelineSession`, `AnalysisFramePublisher`, `GstCommandBuilder`, `PreviewStreamReader`, `DmaBufferAllocator`, `GstProcessRunner`, and `VideoPipelineRunner` consistently across later tasks

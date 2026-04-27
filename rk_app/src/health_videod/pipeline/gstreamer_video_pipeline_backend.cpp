#include "pipeline/gstreamer_video_pipeline_backend.h"

#include "analysis/analysis_output_backend.h"
#include "debug/latency_marker_writer.h"
#include "analysis/shared_memory_frame_ring.h"

#include <QDateTime>
#include <QProcess>
#include <QProcessEnvironment>
#include <linux/dma-heap.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace {
const int kStartTimeoutMs = 5000;
const int kStopTimeoutMs = 5000;
const int kStartupProbeMs = 750;
const int kStableAnalysisTapFps = 15;
const int kAnalysisOutputWidth = 640;
const int kAnalysisOutputHeight = 640;
const quint16 kAnalysisRingSlotCount = 32;
const int kPreviewJpegQuality = 95;
const char kGstLaunchEnvVar[] = "RK_VIDEO_GST_LAUNCH_BIN";
const char kDefaultGstLaunchBinary[] = "gst-launch-1.0";
const char kVideoLatencyMarkerEnvVar[] = "RK_VIDEO_LATENCY_MARKER_PATH";
const char kAnalysisConvertBackendEnvVar[] = "RK_VIDEO_ANALYSIS_CONVERT_BACKEND";
const char kAnalysisDmaHeapEnvVar[] = "RK_VIDEO_ANALYSIS_DMA_HEAP";
const char kDefaultAnalysisDmaHeap[] = "/dev/dma_heap/system-uncached-dma32";


QString errnoMessage(const char *prefix) {
    return QStringLiteral("%1_%2_%3")
        .arg(QString::fromLatin1(prefix))
        .arg(errno)
        .arg(QString::fromLocal8Bit(strerror(errno)));
}

int allocateMemFdBuffer(int bytes, QString *error) {
#ifdef SYS_memfd_create
    const int fd = static_cast<int>(::syscall(SYS_memfd_create, "rk_analysis_dmabuf", MFD_CLOEXEC));
    if (fd < 0) {
        if (error) {
            *error = errnoMessage("analysis_memfd_create_failed");
        }
        return -1;
    }
    if (::ftruncate(fd, bytes) != 0) {
        if (error) {
            *error = errnoMessage("analysis_memfd_truncate_failed");
        }
        ::close(fd);
        return -1;
    }
    if (error) {
        error->clear();
    }
    return fd;
#else
    Q_UNUSED(bytes);
    if (error) {
        *error = QStringLiteral("analysis_memfd_unsupported");
    }
    return -1;
#endif
}

int allocateDmaHeapBuffer(const QString &heapPath, int bytes, QString *error) {
    if (heapPath == QStringLiteral("memfd")) {
        return allocateMemFdBuffer(bytes, error);
    }

    const int heapFd = ::open(heapPath.toUtf8().constData(), O_RDWR | O_CLOEXEC);
    if (heapFd < 0) {
        if (error) {
            *error = errnoMessage("analysis_dma_heap_open_failed");
        }
        return -1;
    }

    dma_heap_allocation_data allocation{};
    allocation.len = static_cast<__u64>(bytes);
    allocation.fd_flags = O_RDWR | O_CLOEXEC;
    allocation.heap_flags = 0;
    if (::ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &allocation) != 0) {
        if (error) {
            *error = errnoMessage("analysis_dma_heap_alloc_failed");
        }
        ::close(heapFd);
        return -1;
    }

    ::close(heapFd);
    if (error) {
        error->clear();
    }
    return static_cast<int>(allocation.fd);
}

bool writePayloadToDmaBuffer(int fd, const QByteArray &payload, QString *error) {
    void *mapped = ::mmap(nullptr, static_cast<size_t>(payload.size()), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        if (error) {
            *error = errnoMessage("analysis_dmabuf_mmap_failed");
        }
        return false;
    }
    memcpy(mapped, payload.constData(), static_cast<size_t>(payload.size()));
    ::munmap(mapped, static_cast<size_t>(payload.size()));
    if (error) {
        error->clear();
    }
    return true;
}

QString analysisDmaHeapPath() {
    const QString heap = qEnvironmentVariable(kAnalysisDmaHeapEnvVar).trimmed();
    return heap.isEmpty() ? QString::fromLatin1(kDefaultAnalysisDmaHeap) : heap;
}

int nv12FrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 3 / 2 : 0;
}

int rgbFrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 3 : 0;
}
}

GstreamerVideoPipelineBackend::GstreamerVideoPipelineBackend() = default;

GstreamerVideoPipelineBackend::~GstreamerVideoPipelineBackend() {
    stopAllPipelines();
}

void GstreamerVideoPipelineBackend::setObserver(VideoPipelineObserver *observer) {
    observer_ = observer;
}

void GstreamerVideoPipelineBackend::setAnalysisFrameSource(AnalysisFrameSource *source) {
    analysisFrameSource_ = source;
}

void GstreamerVideoPipelineBackend::setAnalysisFrameConverter(AnalysisFrameConverter *converter) {
    analysisFrameConverter_ = converter;
}

bool GstreamerVideoPipelineBackend::startPreview(
    const VideoChannelStatus &status, QString *previewUrl, QString *error) {
    const bool enableAnalysisTap = analysisTapEnabled(status);
    const AnalysisConvertBackend backend = analysisConvertBackendForProfile(status.previewProfile);
    return startCommand(status.cameraId, buildPreviewCommand(status), false, previewUrl, error,
        enableAnalysisTap ? status.previewProfile : VideoProfile(), backend);
}

bool GstreamerVideoPipelineBackend::stopPreview(const QString &cameraId, QString *error) {
    return stopActivePipeline(cameraId, error);
}

bool GstreamerVideoPipelineBackend::captureSnapshot(
    const VideoChannelStatus &status, const QString &outputPath, QString *error) {
    return runOneShotCommand(buildSnapshotCommand(status, outputPath), error);
}

bool GstreamerVideoPipelineBackend::startRecording(
    const VideoChannelStatus &status, const QString &outputPath, QString *error) {
    QString previewUrl;
    const bool enableAnalysisTap = analysisTapEnabled(status);
    const AnalysisConvertBackend backend = analysisConvertBackendForProfile(status.recordProfile);
    return startCommand(status.cameraId, buildRecordingCommand(status, outputPath), true,
        &previewUrl, error, enableAnalysisTap ? status.recordProfile : VideoProfile(), backend);
}

bool GstreamerVideoPipelineBackend::stopRecording(const QString &cameraId, QString *error) {
    return stopActivePipeline(cameraId, error);
}

QString GstreamerVideoPipelineBackend::gstLaunchBinary() const {
    const QString overrideBinary = qEnvironmentVariable(kGstLaunchEnvVar);
    return overrideBinary.isEmpty() ? QString::fromUtf8(kDefaultGstLaunchBinary) : overrideBinary;
}

QString GstreamerVideoPipelineBackend::shellQuote(const QString &value) const {
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString GstreamerVideoPipelineBackend::previewUrlForCamera(const QString &cameraId) const {
    return QStringLiteral("tcp://127.0.0.1:%1?transport=tcp_mjpeg&boundary=%2")
        .arg(previewPortForCamera(cameraId))
        .arg(previewBoundaryForCamera(cameraId));
}

QString GstreamerVideoPipelineBackend::previewBoundaryForCamera(const QString &cameraId) const {
    Q_UNUSED(cameraId);
    return QStringLiteral("rkpreview");
}

quint16 GstreamerVideoPipelineBackend::previewPortForCamera(const QString &cameraId) const {
    if (cameraId == QStringLiteral("front_cam")) {
        return 5602;
    }
    return 5699;
}

QString GstreamerVideoPipelineBackend::buildAnalysisTapCommandFragment(
    const VideoChannelStatus &status, const VideoProfile &sourceProfile) const {
    if (!analysisTapEnabled(status)) {
        return QString();
    }

    const int analysisTapFps = status.previewProfile.fps > 0
        ? qMin(status.previewProfile.fps, kStableAnalysisTapFps)
        : kStableAnalysisTapFps;

    if (analysisConvertBackendForProfile(sourceProfile) == AnalysisConvertBackend::Rga) {
        return QStringLiteral(
            " t. ! queue leaky=downstream max-size-buffers=1 ! "
            "videorate drop-only=true ! "
            "video/x-raw,format=NV12,width=%1,height=%2,framerate=%3/1 ! "
            "fdsink fd=1 sync=false")
            .arg(sourceProfile.width)
            .arg(sourceProfile.height)
            .arg(analysisTapFps);
    }

    return QStringLiteral(
        " t. ! queue leaky=downstream max-size-buffers=1 ! "
        "videorate drop-only=true ! "
        "videoconvert ! videoscale ! "
        "video/x-raw,format=RGB,width=%1,height=%2,framerate=%3/1 ! "
        "fdsink fd=1 sync=false")
        .arg(kAnalysisOutputWidth)
        .arg(kAnalysisOutputHeight)
        .arg(analysisTapFps);
}

GstreamerVideoPipelineBackend::AnalysisConvertBackend
GstreamerVideoPipelineBackend::analysisConvertBackendForProfile(const VideoProfile &sourceProfile) const {
    const QString requested = qEnvironmentVariable(kAnalysisConvertBackendEnvVar).trimmed().toLower();
    if (requested == QStringLiteral("gstreamer_cpu") || requested == QStringLiteral("cpu")) {
        return AnalysisConvertBackend::GstreamerCpu;
    }
    if (sourceProfile.pixelFormat.compare(QStringLiteral("NV12"), Qt::CaseInsensitive) != 0) {
        return AnalysisConvertBackend::GstreamerCpu;
    }
    if (requested == QStringLiteral("rga")) {
        return AnalysisConvertBackend::Rga;
    }

#if defined(RKAPP_ENABLE_RGA_ANALYSIS_CONVERT) && RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
    return AnalysisConvertBackend::Rga;
#else
    return AnalysisConvertBackend::GstreamerCpu;
#endif
}

bool GstreamerVideoPipelineBackend::analysisTapEnabled(const VideoChannelStatus &status) const {
    return analysisFrameSource_ && analysisFrameSource_->acceptsFrames(status.cameraId);
}

QString GstreamerVideoPipelineBackend::buildPreviewCommand(const VideoChannelStatus &status) const {
    const QString analysisTap = buildAnalysisTapCommandFragment(status, status.previewProfile);
    if (status.inputMode == QStringLiteral("test_file")) {
        if (!analysisTap.isEmpty()) {
            return QStringLiteral(
                "%1 -q -e filesrc location=%2 ! decodebin name=dec "
                "dec. ! queue ! videoconvert ! videoscale ! "
                "video/x-raw,format=NV12,width=%3,height=%4 ! tee name=t "
                "t. ! queue ! mppjpegenc rc-mode=fixqp q-factor=%5 ! multipartmux boundary=%6 ! "
                "tcpserversink host=127.0.0.1 port=%7%8 "
                "dec. ! queue ! audioconvert ! audioresample ! fakesink sync=false")
                .arg(shellQuote(gstLaunchBinary()))
                .arg(shellQuote(status.testFilePath))
                .arg(status.previewProfile.width)
                .arg(status.previewProfile.height)
                .arg(kPreviewJpegQuality)
                .arg(previewBoundaryForCamera(status.cameraId))
                .arg(previewPortForCamera(status.cameraId))
                .arg(analysisTap);
        }

        return QStringLiteral(
            "%1 -q -e filesrc location=%2 ! decodebin name=dec "
            "dec. ! queue ! videoconvert ! videoscale ! "
            "video/x-raw,format=NV12,width=%3,height=%4 ! mppjpegenc rc-mode=fixqp q-factor=%5 ! multipartmux boundary=%6 ! "
            "tcpserversink host=127.0.0.1 port=%7 "
            "dec. ! queue ! audioconvert ! audioresample ! fakesink sync=false")
            .arg(shellQuote(gstLaunchBinary()))
            .arg(shellQuote(status.testFilePath))
            .arg(status.previewProfile.width)
            .arg(status.previewProfile.height)
            .arg(kPreviewJpegQuality)
            .arg(previewBoundaryForCamera(status.cameraId))
            .arg(previewPortForCamera(status.cameraId));
    }

    if (!analysisTap.isEmpty()) {
        return QStringLiteral(
            "%1 -q -e v4l2src device=%2 ! "
            "video/x-raw,format=%3,width=%4,height=%5,framerate=%6/1 ! "
            "tee name=t "
            "t. ! queue ! mppjpegenc rc-mode=fixqp q-factor=%7 ! multipartmux boundary=%8 ! "
            "tcpserversink host=127.0.0.1 port=%9%10")
            .arg(shellQuote(gstLaunchBinary()))
            .arg(shellQuote(status.devicePath))
            .arg(status.previewProfile.pixelFormat)
            .arg(status.previewProfile.width)
            .arg(status.previewProfile.height)
            .arg(status.previewProfile.fps > 0 ? status.previewProfile.fps : 30)
            .arg(kPreviewJpegQuality)
            .arg(previewBoundaryForCamera(status.cameraId))
            .arg(previewPortForCamera(status.cameraId))
            .arg(analysisTap);
    }

    return QStringLiteral(
        "%1 -q -e v4l2src device=%2 ! "
        "video/x-raw,format=%3,width=%4,height=%5,framerate=%6/1 ! "
        "mppjpegenc rc-mode=fixqp q-factor=%7 ! multipartmux boundary=%8 ! "
        "tcpserversink host=127.0.0.1 port=%9")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.previewProfile.pixelFormat)
        .arg(status.previewProfile.width)
        .arg(status.previewProfile.height)
        .arg(status.previewProfile.fps > 0 ? status.previewProfile.fps : 30)
        .arg(kPreviewJpegQuality)
        .arg(previewBoundaryForCamera(status.cameraId))
        .arg(previewPortForCamera(status.cameraId));
}

QString GstreamerVideoPipelineBackend::buildRecordingCommand(
    const VideoChannelStatus &status, const QString &outputPath) const {
    const QString analysisTap = buildAnalysisTapCommandFragment(status, status.recordProfile);
    return QStringLiteral(
        "%1 -q -e v4l2src device=%2 ! "
        "video/x-raw,format=%3,width=%4,height=%5,framerate=%6/1 ! "
        "tee name=t "
        "t. ! queue ! videoscale ! video/x-raw,format=NV12,width=%7,height=%8 ! "
        "mppjpegenc rc-mode=fixqp q-factor=%9 ! multipartmux boundary=%10 ! "
        "tcpserversink host=127.0.0.1 port=%11%12 "
        "t. ! queue ! mpph264enc ! h264parse ! qtmux ! filesink location=%13")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.recordProfile.pixelFormat)
        .arg(status.recordProfile.width)
        .arg(status.recordProfile.height)
        .arg(status.recordProfile.fps > 0 ? status.recordProfile.fps : 30)
        .arg(status.previewProfile.width)
        .arg(status.previewProfile.height)
        .arg(kPreviewJpegQuality)
        .arg(previewBoundaryForCamera(status.cameraId))
        .arg(previewPortForCamera(status.cameraId))
        .arg(analysisTap)
        .arg(shellQuote(outputPath));
}

QString GstreamerVideoPipelineBackend::buildSnapshotCommand(
    const VideoChannelStatus &status, const QString &outputPath) const {
    return QStringLiteral(
        "%1 -q -e v4l2src device=%2 num-buffers=1 ! "
        "video/x-raw,format=%3,width=%4,height=%5 ! mppjpegenc ! filesink location=%6")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.snapshotProfile.pixelFormat)
        .arg(status.snapshotProfile.width)
        .arg(status.snapshotProfile.height)
        .arg(shellQuote(outputPath));
}

void GstreamerVideoPipelineBackend::processAnalysisStdout(const QString &cameraId) {
    if (!pipelines_.contains(cameraId)) {
        return;
    }

    ActivePipeline &pipeline = pipelines_[cameraId];
    if (!pipeline.process || pipeline.analysisInputFrameBytes <= 0) {
        if (pipeline.process) {
            pipeline.process->readAllStandardOutput();
        }
        return;
    }

    pipeline.stdoutBuffer.append(pipeline.process->readAllStandardOutput());
    while (pipeline.stdoutBuffer.size() >= pipeline.analysisInputFrameBytes) {
        const QByteArray inputFrame = pipeline.stdoutBuffer.left(pipeline.analysisInputFrameBytes);
        pipeline.stdoutBuffer.remove(0, pipeline.analysisInputFrameBytes);

        QByteArray rgbPayload;
        AnalysisFrameConversionMetadata conversionMetadata;
        if (pipeline.analysisConvertBackend == AnalysisConvertBackend::Rga) {
            AnalysisFrameConverter *converter = analysisFrameConverter_
                ? analysisFrameConverter_
                : &defaultRgaFrameConverter_;
            QString convertError;
            if (!converter->convertNv12ToRgb(inputFrame,
                    pipeline.analysisInputWidth,
                    pipeline.analysisInputHeight,
                    pipeline.analysisOutputWidth,
                    pipeline.analysisOutputHeight,
                    &rgbPayload,
                    &conversionMetadata,
                    &convertError)) {
                qWarning().noquote()
                    << QStringLiteral("video_runtime camera=%1 event=analysis_convert_failed backend=rga error=%2")
                           .arg(cameraId)
                           .arg(convertError.isEmpty() ? QStringLiteral("unknown") : convertError);
                continue;
            }
        } else {
            rgbPayload = inputFrame;
        }

        if (rgbPayload.size() != pipeline.analysisOutputFrameBytes) {
            qWarning().noquote()
                << QStringLiteral("video_runtime camera=%1 event=analysis_frame_size_mismatch expected=%2 actual=%3")
                       .arg(cameraId)
                       .arg(pipeline.analysisOutputFrameBytes)
                       .arg(rgbPayload.size());
            continue;
        }

        AnalysisFramePacket packet;
        packet.frameId = pipeline.nextFrameId++;
        packet.timestampMs = QDateTime::currentMSecsSinceEpoch();
        packet.cameraId = pipeline.cameraId;
        packet.width = pipeline.analysisOutputWidth;
        packet.height = pipeline.analysisOutputHeight;
        packet.pixelFormat = AnalysisPixelFormat::Rgb;
        packet.posePreprocessed = conversionMetadata.posePreprocessed;
        packet.poseXPad = conversionMetadata.poseXPad;
        packet.poseYPad = conversionMetadata.poseYPad;
        packet.poseScale = conversionMetadata.poseScale;
        packet.payload = rgbPayload;

        if (analysisFrameSource_ && analysisFrameSource_->acceptsFrames(packet.cameraId)
            && pipeline.frameRing) {
            AnalysisFrameDescriptor descriptor;
            descriptor.frameId = packet.frameId;
            descriptor.timestampMs = packet.timestampMs;
            descriptor.cameraId = packet.cameraId;
            descriptor.width = packet.width;
            descriptor.height = packet.height;
            descriptor.pixelFormat = packet.pixelFormat;
            descriptor.posePreprocessed = packet.posePreprocessed;
            descriptor.poseXPad = packet.poseXPad;
            descriptor.poseYPad = packet.poseYPad;
            descriptor.poseScale = packet.poseScale;
            descriptor.payloadBytes = static_cast<quint32>(packet.payload.size());

            bool publishedViaDmaBuf = false;
            if (analysisFrameSource_->supportsDmaBufFrames()) {
                QString dmaError;
                const int dmaFd = allocateDmaHeapBuffer(analysisDmaHeapPath(), packet.payload.size(), &dmaError);
                if (dmaFd >= 0 && writePayloadToDmaBuffer(dmaFd, packet.payload, &dmaError)) {
                    descriptor.payloadTransport = AnalysisPayloadTransport::DmaBuf;
                    descriptor.dmaBufPlaneCount = 1;
                    descriptor.dmaBufOffset = 0;
                    descriptor.dmaBufStrideBytes = static_cast<quint32>(packet.width * 3);
                    descriptor.sequence = packet.frameId;
                    analysisFrameSource_->publishDmaBufDescriptor(descriptor, dmaFd);
                    publishedViaDmaBuf = true;
                } else {
                    qWarning().noquote()
                        << QStringLiteral("video_runtime camera=%1 event=analysis_dmabuf_publish_failed error=%2")
                               .arg(cameraId)
                               .arg(dmaError.isEmpty() ? QStringLiteral("unknown") : dmaError);
                }
                if (dmaFd >= 0) {
                    ::close(dmaFd);
                }
            }

            if (!publishedViaDmaBuf) {
                const SharedFramePublishResult publish = pipeline.frameRing->publish(packet);
                if (publish.sequence == 0) {
                    continue;
                }
                descriptor.payloadTransport = AnalysisPayloadTransport::SharedMemory;
                descriptor.dmaBufPlaneCount = 0;
                descriptor.dmaBufOffset = 0;
                descriptor.dmaBufStrideBytes = 0;
                descriptor.slotIndex = publish.slotIndex;
                descriptor.sequence = publish.sequence;
                descriptor.payloadBytes = publish.payloadBytes;
                analysisFrameSource_->publishDescriptor(descriptor);
            }

            bool streamConnected = false;
            if (auto *outputBackend = dynamic_cast<AnalysisOutputBackend *>(analysisFrameSource_)) {
                streamConnected = outputBackend->statusForCamera(packet.cameraId).streamConnected;
            }
            pipeline.logStats.onDescriptorPublished(
                packet.cameraId,
                pipeline.testInput ? QStringLiteral("test_file") : QStringLiteral("camera"),
                streamConnected,
                pipeline.frameRing->droppedFrames(),
                packet.timestampMs);
            if (const auto summary = pipeline.logStats.takeSummaryIfDue(packet.timestampMs)) {
                qInfo().noquote()
                    << QStringLiteral(
                           "video_perf camera=%1 mode=%2 state=%3 fps=%4 published=%5 dropped_total=%6 dropped_delta=%7 consumers=%8")
                           .arg(summary->cameraId)
                           .arg(summary->inputMode)
                           .arg(pipeline.recording ? QStringLiteral("recording")
                                                   : QStringLiteral("previewing"))
                           .arg(QString::number(summary->publishFps, 'f', 1))
                           .arg(summary->publishedFramesWindow)
                           .arg(summary->droppedFramesTotal)
                           .arg(summary->droppedFramesDelta)
                           .arg(summary->consumerConnected ? 1 : 0);
            }

            LatencyMarkerWriter marker(qEnvironmentVariable(kVideoLatencyMarkerEnvVar));
            marker.writeEvent(QStringLiteral("analysis_descriptor_published"), packet.timestampMs,
                QJsonObject{
                    {QStringLiteral("camera_id"), packet.cameraId},
                    {QStringLiteral("frame_id"), QString::number(packet.frameId)},
                    {QStringLiteral("slot_index"), static_cast<int>(descriptor.slotIndex)},
                    {QStringLiteral("sequence"), QString::number(descriptor.sequence)},
                    {QStringLiteral("transport"), descriptor.payloadTransport == AnalysisPayloadTransport::DmaBuf
                            ? QStringLiteral("dmabuf")
                            : QStringLiteral("shared_memory")},
                    {QStringLiteral("dropped_frames"),
                        static_cast<double>(pipeline.frameRing->droppedFrames())},
                });
        }
    }
}

bool GstreamerVideoPipelineBackend::startCommand(const QString &cameraId, const QString &command,
    bool recording, QString *previewUrl, QString *error, const VideoProfile &analysisInputProfile,
    AnalysisConvertBackend analysisConvertBackend) {
    if (error) {
        error->clear();
    }

    QString stopError;
    if (!stopActivePipeline(cameraId, &stopError) && !stopError.isEmpty()) {
        if (error) {
            *error = stopError;
        }
        return false;
    }

    auto *process = new QProcess();
    process->setProgram(QStringLiteral("/bin/bash"));
    process->setArguments({QStringLiteral("-lc"), QStringLiteral("exec %1").arg(command)});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    QObject::connect(process,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, cameraId, process](int exitCode, QProcess::ExitStatus exitStatus) {
            if (!pipelines_.contains(cameraId) || pipelines_.value(cameraId).process != process) {
                return;
            }

            const ActivePipeline pipeline = pipelines_.take(cameraId);
            const bool testInput = pipeline.testInput;
            delete pipeline.frameRing;
            delete process;

            if (!observer_) {
                return;
            }
            if (testInput && exitStatus == QProcess::NormalExit && exitCode == 0) {
                qInfo().noquote()
                    << QStringLiteral("video_runtime camera=%1 event=playback_finished")
                           .arg(cameraId);
                observer_->onPipelinePlaybackFinished(cameraId);
                return;
            }
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                qWarning().noquote()
                    << QStringLiteral("video_runtime camera=%1 event=pipeline_error error=%2")
                           .arg(cameraId)
                           .arg(QStringLiteral("preview_pipeline_failed"));
                observer_->onPipelineRuntimeError(cameraId, QStringLiteral("preview_pipeline_failed"));
            }
        });
    QObject::connect(process, &QProcess::readyReadStandardOutput, [this, cameraId]() {
        processAnalysisStdout(cameraId);
    });

    process->start();
    if (!process->waitForStarted(kStartTimeoutMs)) {
        if (error) {
            *error = process->errorString();
        }
        delete process;
        return false;
    }
    if (process->waitForFinished(kStartupProbeMs)) {
        if (error) {
            const QString startupOutput = QString::fromUtf8(process->readAllStandardError()).trimmed();
            *error = startupOutput.isEmpty()
                ? QStringLiteral("pipeline_exited_during_startup")
                : startupOutput;
        }
        delete process;
        return false;
    }

    ActivePipeline pipeline;
    pipeline.process = process;
    pipeline.recording = recording;
    pipeline.testInput = command.contains(QStringLiteral("filesrc location="));
    pipeline.previewUrl = previewUrlForCamera(cameraId);
    pipeline.cameraId = cameraId;
    pipeline.analysisConvertBackend = analysisConvertBackend;
    pipeline.analysisInputWidth = analysisInputProfile.width;
    pipeline.analysisInputHeight = analysisInputProfile.height;
    pipeline.analysisInputFrameBytes = 0;
    if (analysisInputProfile.width > 0 && analysisInputProfile.height > 0) {
        pipeline.analysisInputFrameBytes = analysisConvertBackend == AnalysisConvertBackend::Rga
            ? nv12FrameBytes(analysisInputProfile.width, analysisInputProfile.height)
            : rgbFrameBytes(kAnalysisOutputWidth, kAnalysisOutputHeight);
        pipeline.analysisOutputWidth = kAnalysisOutputWidth;
        pipeline.analysisOutputHeight = kAnalysisOutputHeight;
        pipeline.analysisOutputFrameBytes = rgbFrameBytes(kAnalysisOutputWidth, kAnalysisOutputHeight);
    }
    if (pipeline.analysisOutputFrameBytes > 0) {
        pipeline.frameRing = new SharedMemoryFrameRingWriter(
            cameraId, kAnalysisRingSlotCount, static_cast<quint32>(pipeline.analysisOutputFrameBytes));
        QString ringError;
        if (!pipeline.frameRing->initialize(&ringError)) {
            const QString finalError = ringError.isEmpty()
                ? QStringLiteral("analysis_ring_init_failed")
                : ringError;
            qWarning().noquote()
                << QStringLiteral("video_runtime camera=%1 event=analysis_ring_init_failed error=%2")
                       .arg(cameraId)
                       .arg(finalError);
            if (error) {
                *error = finalError;
            }
            delete pipeline.frameRing;
            process->kill();
            process->waitForFinished(kStopTimeoutMs);
            delete process;
            return false;
        }
    }
    pipelines_.insert(cameraId, pipeline);
    processAnalysisStdout(cameraId);

    qInfo().noquote()
        << QStringLiteral("video_runtime camera=%1 event=preview_started mode=%2 analysis=%3 analysis_backend=%4")
               .arg(cameraId)
               .arg(pipeline.testInput ? QStringLiteral("test_file") : QStringLiteral("camera"))
               .arg(pipeline.analysisOutputFrameBytes > 0 ? 1 : 0)
               .arg(pipeline.analysisOutputFrameBytes <= 0
                       ? QStringLiteral("off")
                       : (pipeline.analysisConvertBackend == AnalysisConvertBackend::Rga
                               ? QStringLiteral("rga")
                               : QStringLiteral("gstreamer_cpu")));

    if (previewUrl) {
        *previewUrl = pipeline.previewUrl;
    }
    return true;
}

bool GstreamerVideoPipelineBackend::runOneShotCommand(const QString &command, QString *error) const {
    QProcess process;
    process.setProgram(QStringLiteral("/bin/bash"));
    process.setArguments({QStringLiteral("-lc"), command});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(kStartTimeoutMs)) {
        if (error) {
            *error = process.errorString();
        }
        return false;
    }
    if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        if (error) {
            *error = QString::fromUtf8(process.readAll()).trimmed();
            if (error->isEmpty()) {
                *error = QStringLiteral("snapshot_command_failed");
            }
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool GstreamerVideoPipelineBackend::stopActivePipeline(const QString &cameraId, QString *error) {
    if (error) {
        error->clear();
    }

    if (!pipelines_.contains(cameraId)) {
        return true;
    }

    ActivePipeline pipeline = pipelines_.take(cameraId);
    if (!pipeline.process) {
        return true;
    }

    const qint64 processId = pipeline.process->processId();
    if (processId > 0) {
        ::kill(static_cast<pid_t>(processId), SIGINT);
    }
    if (!pipeline.process->waitForFinished(kStopTimeoutMs)) {
        pipeline.process->kill();
        pipeline.process->waitForFinished(kStopTimeoutMs);
    }
    if (pipeline.process->state() != QProcess::NotRunning && error) {
        *error = QStringLiteral("pipeline_stop_failed");
    }
    qInfo().noquote()
        << QStringLiteral("video_runtime camera=%1 event=preview_stopped").arg(cameraId);
    delete pipeline.frameRing;
    delete pipeline.process;
    return error ? error->isEmpty() : true;
}

void GstreamerVideoPipelineBackend::stopAllPipelines() {
    const auto cameraIds = pipelines_.keys();
    for (const QString &cameraId : cameraIds) {
        QString error;
        stopActivePipeline(cameraId, &error);
    }
}

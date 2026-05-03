#include "pipeline/analysis_frame_publisher.h"

#include "analysis/analysis_frame_source.h"
#include "analysis/analysis_output_backend.h"
#include "analysis/shared_memory_frame_ring.h"
#include "debug/latency_marker_writer.h"
#include "pipeline/dma_buffer_allocator.h"
#include "pipeline/pipeline_session.h"

#include <QDateTime>
#include <QJsonObject>
#include <QLoggingCategory>

#include <unistd.h>

namespace {
const char kDefaultAnalysisDmaHeap[] = "/dev/dma_heap/system-uncached-dma32";
}

AnalysisFramePublisher::AnalysisFramePublisher(
    const AppRuntimeConfig &runtimeConfig, const DmaBufferAllocator *dmaBufferAllocator)
    : runtimeConfig_(runtimeConfig)
    , dmaBufferAllocator_(dmaBufferAllocator) {
}

void AnalysisFramePublisher::setFrameSource(AnalysisFrameSource *source) {
    frameSource_ = source;
}

void AnalysisFramePublisher::setFrameConverter(AnalysisFrameConverter *converter) {
    frameConverter_ = converter;
}

void AnalysisFramePublisher::setFallbackFrameConverter(AnalysisFrameConverter *converter) {
    fallbackFrameConverter_ = converter;
}

AnalysisFrameConverter *AnalysisFramePublisher::activeConverter() const {
    return frameConverter_ ? frameConverter_ : fallbackFrameConverter_;
}

bool AnalysisFramePublisher::publishDmaDescriptor(PipelineSession *session,
    qint64 timestampMs,
    const AnalysisDmaBuffer &dmaBuffer,
    const AnalysisFrameConversionMetadata &conversionMetadata,
    bool rgaInputDmabuf,
    bool rgaOutputDmabuf) const {
    if (!session || !frameSource_ || !frameSource_->supportsDmaBufFrames()) {
        return false;
    }

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = session->nextFrameId++;
    descriptor.timestampMs = timestampMs;
    descriptor.cameraId = session->cameraId;
    descriptor.width = session->analysisOutputWidth;
    descriptor.height = session->analysisOutputHeight;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.posePreprocessed = conversionMetadata.posePreprocessed;
    descriptor.poseXPad = conversionMetadata.poseXPad;
    descriptor.poseYPad = conversionMetadata.poseYPad;
    descriptor.poseScale = conversionMetadata.poseScale;
    descriptor.payloadTransport = AnalysisPayloadTransport::DmaBuf;
    descriptor.dmaBufPlaneCount = 1;
    descriptor.dmaBufOffset = dmaBuffer.offset;
    descriptor.dmaBufStrideBytes = dmaBuffer.strideBytes;
    descriptor.sequence = descriptor.frameId;
    descriptor.payloadBytes = dmaBuffer.payloadBytes;

    frameSource_->publishDmaBufDescriptor(descriptor, dmaBuffer.fd);
    onDescriptorPublished(session, descriptor, timestampMs);

    LatencyMarkerWriter marker(runtimeConfig_.debug.videoLatencyMarkerPath);
    marker.writeEvent(QStringLiteral("analysis_descriptor_published"), descriptor.timestampMs,
        QJsonObject{
            {QStringLiteral("camera_id"), descriptor.cameraId},
            {QStringLiteral("frame_id"), QString::number(descriptor.frameId)},
            {QStringLiteral("slot_index"), static_cast<int>(descriptor.slotIndex)},
            {QStringLiteral("sequence"), QString::number(descriptor.sequence)},
            {QStringLiteral("transport"), QStringLiteral("dmabuf")},
            {QStringLiteral("rga_input_dmabuf"), rgaInputDmabuf},
            {QStringLiteral("rga_output_dmabuf"), rgaOutputDmabuf},
            {QStringLiteral("dropped_frames"),
                static_cast<double>(session->frameRing ? session->frameRing->droppedFrames() : 0)},
        });
    return true;
}

bool AnalysisFramePublisher::publishSharedMemoryOrDma(PipelineSession *session,
    qint64 timestampMs,
    const QByteArray &rgbPayload,
    const AnalysisFrameConversionMetadata &conversionMetadata) const {
    if (!session || !frameSource_ || !frameSource_->acceptsFrames(session->cameraId)
        || !session->frameRing) {
        return false;
    }

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = session->nextFrameId++;
    descriptor.timestampMs = timestampMs;
    descriptor.cameraId = session->cameraId;
    descriptor.width = session->analysisOutputWidth;
    descriptor.height = session->analysisOutputHeight;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.posePreprocessed = conversionMetadata.posePreprocessed;
    descriptor.poseXPad = conversionMetadata.poseXPad;
    descriptor.poseYPad = conversionMetadata.poseYPad;
    descriptor.poseScale = conversionMetadata.poseScale;
    descriptor.payloadBytes = static_cast<quint32>(rgbPayload.size());

    bool publishedViaDmaBuf = false;
    int dmaFd = -1;
    if (frameSource_->supportsDmaBufFrames() && dmaBufferAllocator_) {
        QString dmaError;
        const QString dmaHeapPath = runtimeConfig_.analysis.dmaHeap.trimmed().isEmpty()
            ? QString::fromLatin1(kDefaultAnalysisDmaHeap)
            : runtimeConfig_.analysis.dmaHeap.trimmed();
        dmaFd = dmaBufferAllocator_->allocate(dmaHeapPath, rgbPayload.size(), &dmaError);
        if (dmaFd >= 0 && dmaBufferAllocator_->writePayload(dmaFd, rgbPayload, &dmaError)) {
            descriptor.payloadTransport = AnalysisPayloadTransport::DmaBuf;
            descriptor.dmaBufPlaneCount = 1;
            descriptor.dmaBufOffset = 0;
            descriptor.dmaBufStrideBytes = static_cast<quint32>(session->analysisOutputWidth * 3);
            descriptor.sequence = descriptor.frameId;
            frameSource_->publishDmaBufDescriptor(descriptor, dmaFd);
            publishedViaDmaBuf = true;
        } else {
            qWarning().noquote()
                << QStringLiteral("video_runtime camera=%1 event=analysis_dmabuf_publish_failed error=%2")
                       .arg(session->cameraId)
                       .arg(dmaError.isEmpty() ? QStringLiteral("unknown") : dmaError);
        }
    }

    if (!publishedViaDmaBuf) {
        const AnalysisFramePacket packet{
            descriptor.frameId,
            descriptor.timestampMs,
            descriptor.cameraId,
            descriptor.width,
            descriptor.height,
            descriptor.pixelFormat,
            descriptor.posePreprocessed,
            descriptor.poseXPad,
            descriptor.poseYPad,
            descriptor.poseScale,
            AnalysisPayloadTransport::SharedMemory,
            0,
            0,
            0,
            AnalysisDmaBufPayloadPtr(),
            rgbPayload,
        };
        const SharedFramePublishResult publish = session->frameRing->publish(packet);
        if (publish.sequence == 0) {
            if (dmaFd >= 0) {
                ::close(dmaFd);
            }
            return false;
        }

        descriptor.payloadTransport = AnalysisPayloadTransport::SharedMemory;
        descriptor.dmaBufPlaneCount = 0;
        descriptor.dmaBufOffset = 0;
        descriptor.dmaBufStrideBytes = 0;
        descriptor.slotIndex = publish.slotIndex;
        descriptor.sequence = publish.sequence;
        descriptor.payloadBytes = publish.payloadBytes;
        frameSource_->publishDescriptor(descriptor);
    }

    if (dmaFd >= 0) {
        ::close(dmaFd);
    }

    onDescriptorPublished(session, descriptor, timestampMs);
    LatencyMarkerWriter marker(runtimeConfig_.debug.videoLatencyMarkerPath);
    marker.writeEvent(QStringLiteral("analysis_descriptor_published"), descriptor.timestampMs,
        QJsonObject{
            {QStringLiteral("camera_id"), descriptor.cameraId},
            {QStringLiteral("frame_id"), QString::number(descriptor.frameId)},
            {QStringLiteral("slot_index"), static_cast<int>(descriptor.slotIndex)},
            {QStringLiteral("sequence"), QString::number(descriptor.sequence)},
            {QStringLiteral("transport"), descriptor.payloadTransport == AnalysisPayloadTransport::DmaBuf
                    ? QStringLiteral("dmabuf")
                    : QStringLiteral("shared_memory")},
            {QStringLiteral("dropped_frames"),
                static_cast<double>(session->frameRing ? session->frameRing->droppedFrames() : 0)},
        });
    return true;
}

void AnalysisFramePublisher::onDescriptorPublished(
    PipelineSession *session, const AnalysisFrameDescriptor &descriptor, qint64 timestampMs) const {
    if (!session) {
        return;
    }

    bool streamConnected = false;
    if (auto *outputBackend = dynamic_cast<AnalysisOutputBackend *>(frameSource_)) {
        streamConnected = outputBackend->statusForCamera(descriptor.cameraId).streamConnected;
    }
    session->logStats.onDescriptorPublished(descriptor.cameraId, session->inputModeName(),
        streamConnected, session->frameRing ? session->frameRing->droppedFrames() : 0, timestampMs);
    if (const auto summary = session->logStats.takeSummaryIfDue(timestampMs)) {
        qInfo().noquote()
            << QStringLiteral(
                   "video_perf camera=%1 mode=%2 state=%3 fps=%4 published=%5 dropped_total=%6 dropped_delta=%7 consumers=%8")
                   .arg(summary->cameraId)
                   .arg(summary->inputMode)
                   .arg(session->pipelineStateName())
                   .arg(QString::number(summary->publishFps, 'f', 1))
                   .arg(summary->publishedFramesWindow)
                   .arg(summary->droppedFramesTotal)
                   .arg(summary->droppedFramesDelta)
                   .arg(summary->consumerConnected ? 1 : 0);
    }
}

bool AnalysisFramePublisher::publishFrameDma(
    PipelineSession *session, const AnalysisDmaBuffer &inputFrame) const {
    if (!session || !frameSource_ || !frameSource_->supportsDmaBufFrames()) {
        return false;
    }
    if (session->analysisInputFrameBytes <= 0
        || session->analysisConvertBackend != AnalysisConvertBackend::Rga
        || !runtimeConfig_.analysis.rgaOutputDmabuf) {
        return false;
    }

    AnalysisFrameConverter *converter = activeConverter();
    if (!converter) {
        return false;
    }

    AnalysisDmaBuffer outputBuffer;
    AnalysisFrameConversionMetadata conversionMetadata;
    QString convertError;
    const bool dmaConverted = inputFrame.inputFormat == AnalysisFrameInputFormat::Uyvy
        ? converter->convertUyvyDmaToRgbDma(inputFrame,
            session->analysisInputWidth,
            session->analysisInputHeight,
            session->analysisOutputWidth,
            session->analysisOutputHeight,
            &outputBuffer,
            &conversionMetadata,
            &convertError)
        : converter->convertNv12DmaToRgbDma(inputFrame,
            session->analysisInputWidth,
            session->analysisInputHeight,
            session->analysisOutputWidth,
            session->analysisOutputHeight,
            &outputBuffer,
            &conversionMetadata,
            &convertError);
    if (!dmaConverted) {
        qWarning().noquote()
            << QStringLiteral("video_runtime camera=%1 event=analysis_dma_input_output_convert_failed error=%2")
                   .arg(session->cameraId)
                   .arg(convertError.isEmpty() ? QStringLiteral("unknown") : convertError);
        return false;
    }

    const qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
    const bool published = publishDmaDescriptor(
        session, timestampMs, outputBuffer, conversionMetadata, true, true);
    ::close(outputBuffer.fd);
    return published;
}

void AnalysisFramePublisher::publishFrameBytes(
    PipelineSession *session, const QByteArray &inputFrame) const {
    if (!session || session->analysisInputFrameBytes <= 0) {
        return;
    }

    QByteArray rgbPayload;
    AnalysisFrameConversionMetadata conversionMetadata;
    if (session->analysisConvertBackend == AnalysisConvertBackend::Rga) {
        AnalysisFrameConverter *converter = activeConverter();
        if (!converter) {
            return;
        }

        if (runtimeConfig_.analysis.rgaOutputDmabuf && frameSource_
            && frameSource_->supportsDmaBufFrames()) {
            AnalysisDmaBuffer dmaBuffer;
            QString dmaConvertError;
            const bool dmaOutputConverted = session->analysisInputFormat == AnalysisFrameInputFormat::Uyvy
                ? converter->convertUyvyToRgbDma(inputFrame,
                    session->analysisInputWidth,
                    session->analysisInputHeight,
                    session->analysisOutputWidth,
                    session->analysisOutputHeight,
                    &dmaBuffer,
                    &conversionMetadata,
                    &dmaConvertError)
                : converter->convertNv12ToRgbDma(inputFrame,
                    session->analysisInputWidth,
                    session->analysisInputHeight,
                    session->analysisOutputWidth,
                    session->analysisOutputHeight,
                    &dmaBuffer,
                    &conversionMetadata,
                    &dmaConvertError);
            if (dmaOutputConverted) {
                const qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
                const bool published = publishDmaDescriptor(
                    session, timestampMs, dmaBuffer, conversionMetadata, false, true);
                ::close(dmaBuffer.fd);
                if (published) {
                    return;
                }
            } else {
                qWarning().noquote()
                    << QStringLiteral("video_runtime camera=%1 event=analysis_dma_output_convert_failed error=%2")
                           .arg(session->cameraId)
                           .arg(dmaConvertError.isEmpty() ? QStringLiteral("unknown") : dmaConvertError);
            }
        }

        QString convertError;
        const bool converted = session->analysisInputFormat == AnalysisFrameInputFormat::Uyvy
            ? converter->convertUyvyToRgb(inputFrame,
                session->analysisInputWidth,
                session->analysisInputHeight,
                session->analysisOutputWidth,
                session->analysisOutputHeight,
                &rgbPayload,
                &conversionMetadata,
                &convertError)
            : converter->convertNv12ToRgb(inputFrame,
                session->analysisInputWidth,
                session->analysisInputHeight,
                session->analysisOutputWidth,
                session->analysisOutputHeight,
                &rgbPayload,
                &conversionMetadata,
                &convertError);
        if (!converted) {
            qWarning().noquote()
                << QStringLiteral("video_runtime camera=%1 event=analysis_convert_failed backend=rga error=%2")
                       .arg(session->cameraId)
                       .arg(convertError.isEmpty() ? QStringLiteral("unknown") : convertError);
            return;
        }
    } else {
        rgbPayload = inputFrame;
    }

    if (rgbPayload.size() != session->analysisOutputFrameBytes) {
        qWarning().noquote()
            << QStringLiteral("video_runtime camera=%1 event=analysis_frame_size_mismatch expected=%2 actual=%3")
                   .arg(session->cameraId)
                   .arg(session->analysisOutputFrameBytes)
                   .arg(rgbPayload.size());
        return;
    }

    const qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
    publishSharedMemoryOrDma(session, timestampMs, rgbPayload, conversionMetadata);
}

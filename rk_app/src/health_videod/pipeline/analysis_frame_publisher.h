#pragma once

#include "analysis/analysis_frame_converter.h"
#include "models/fall_models.h"
#include "runtime_config/app_runtime_config.h"

#include <QByteArray>

class DmaBufferAllocator;
class AnalysisFrameSource;
struct PipelineSession;

class AnalysisFramePublisher {
public:
    AnalysisFramePublisher(
        const AppRuntimeConfig &runtimeConfig, const DmaBufferAllocator *dmaBufferAllocator);

    void setFrameSource(AnalysisFrameSource *source);
    void setFrameConverter(AnalysisFrameConverter *converter);
    void setFallbackFrameConverter(AnalysisFrameConverter *converter);

    void publishFrameBytes(PipelineSession *session, const QByteArray &inputFrame) const;
    bool publishFrameDma(PipelineSession *session, const AnalysisDmaBuffer &inputFrame) const;

private:
    AnalysisFrameConverter *activeConverter() const;
    bool publishSharedMemoryOrDma(PipelineSession *session,
        qint64 timestampMs,
        const QByteArray &rgbPayload,
        const AnalysisFrameConversionMetadata &conversionMetadata) const;
    bool publishDmaDescriptor(PipelineSession *session,
        qint64 timestampMs,
        const AnalysisDmaBuffer &dmaBuffer,
        const AnalysisFrameConversionMetadata &conversionMetadata,
        bool rgaInputDmabuf,
        bool rgaOutputDmabuf) const;
    void onDescriptorPublished(
        PipelineSession *session, const AnalysisFrameDescriptor &descriptor, qint64 timestampMs) const;

    AppRuntimeConfig runtimeConfig_;
    const DmaBufferAllocator *dmaBufferAllocator_ = nullptr;
    AnalysisFrameSource *frameSource_ = nullptr;
    AnalysisFrameConverter *frameConverter_ = nullptr;
    AnalysisFrameConverter *fallbackFrameConverter_ = nullptr;
};

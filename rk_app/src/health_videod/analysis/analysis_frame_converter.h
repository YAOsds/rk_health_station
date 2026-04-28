#pragma once

#include <QByteArray>
#include <QString>

struct AnalysisFrameConversionMetadata {
    bool posePreprocessed = false;
    qint32 poseXPad = 0;
    qint32 poseYPad = 0;
    float poseScale = 1.0f;
};

enum class AnalysisFrameInputFormat {
    Nv12,
    Uyvy,
};

struct AnalysisDmaBuffer {
    // Input fds are borrowed by converters; output fds are owned by the caller.
    int fd = -1;
    AnalysisFrameInputFormat inputFormat = AnalysisFrameInputFormat::Nv12;
    quint32 payloadBytes = 0;
    quint32 offset = 0;
    quint32 strideBytes = 0;
};

class AnalysisFrameConverter {
public:
    virtual ~AnalysisFrameConverter() = default;

    virtual bool convertNv12ToRgb(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        QByteArray *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) = 0;

    virtual bool convertNv12ToRgbDma(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) {
        Q_UNUSED(nv12);
        Q_UNUSED(srcWidth);
        Q_UNUSED(srcHeight);
        Q_UNUSED(dstWidth);
        Q_UNUSED(dstHeight);
        Q_UNUSED(rgb);
        Q_UNUSED(metadata);
        if (error) {
            *error = QStringLiteral("analysis_dma_output_not_supported");
        }
        return false;
    }

    virtual bool convertUyvyToRgb(const QByteArray &uyvy,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        QByteArray *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) {
        Q_UNUSED(uyvy);
        Q_UNUSED(srcWidth);
        Q_UNUSED(srcHeight);
        Q_UNUSED(dstWidth);
        Q_UNUSED(dstHeight);
        Q_UNUSED(rgb);
        Q_UNUSED(metadata);
        if (error) {
            *error = QStringLiteral("analysis_uyvy_convert_not_supported");
        }
        return false;
    }

    virtual bool convertUyvyToRgbDma(const QByteArray &uyvy,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) {
        Q_UNUSED(uyvy);
        Q_UNUSED(srcWidth);
        Q_UNUSED(srcHeight);
        Q_UNUSED(dstWidth);
        Q_UNUSED(dstHeight);
        Q_UNUSED(rgb);
        Q_UNUSED(metadata);
        if (error) {
            *error = QStringLiteral("analysis_uyvy_dma_output_not_supported");
        }
        return false;
    }

    virtual bool convertNv12DmaToRgbDma(const AnalysisDmaBuffer &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) {
        Q_UNUSED(nv12);
        Q_UNUSED(srcWidth);
        Q_UNUSED(srcHeight);
        Q_UNUSED(dstWidth);
        Q_UNUSED(dstHeight);
        Q_UNUSED(rgb);
        Q_UNUSED(metadata);
        if (error) {
            *error = QStringLiteral("analysis_dma_input_output_not_supported");
        }
        return false;
    }

    virtual bool convertUyvyDmaToRgbDma(const AnalysisDmaBuffer &uyvy,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) {
        Q_UNUSED(uyvy);
        Q_UNUSED(srcWidth);
        Q_UNUSED(srcHeight);
        Q_UNUSED(dstWidth);
        Q_UNUSED(dstHeight);
        Q_UNUSED(rgb);
        Q_UNUSED(metadata);
        if (error) {
            *error = QStringLiteral("analysis_uyvy_dma_input_output_not_supported");
        }
        return false;
    }
};

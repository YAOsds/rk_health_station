#pragma once

#include "analysis/analysis_frame_converter.h"

class RgaFrameConverter : public AnalysisFrameConverter {
public:
    bool convertNv12ToRgb(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        QByteArray *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override;

    bool convertNv12ToRgbDma(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override;

    bool convertUyvyToRgb(const QByteArray &uyvy,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        QByteArray *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override;

    bool convertUyvyToRgbDma(const QByteArray &uyvy,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override;

    bool convertNv12DmaToRgbDma(const AnalysisDmaBuffer &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override;

    bool convertUyvyDmaToRgbDma(const AnalysisDmaBuffer &uyvy,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override;
};

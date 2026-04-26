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
        QString *error) override;
};

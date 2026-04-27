#pragma once

#include <QByteArray>
#include <QString>

struct AnalysisFrameConversionMetadata {
    bool posePreprocessed = false;
    qint32 poseXPad = 0;
    qint32 poseYPad = 0;
    float poseScale = 1.0f;
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
};

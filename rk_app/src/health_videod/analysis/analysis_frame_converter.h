#pragma once

#include <QByteArray>
#include <QString>

class AnalysisFrameConverter {
public:
    virtual ~AnalysisFrameConverter() = default;

    virtual bool convertNv12ToRgb(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        QByteArray *rgb,
        QString *error) = 0;
};

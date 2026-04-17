#pragma once

#include "models/fall_models.h"
#include "models/video_models.h"

#include <QString>

class AnalysisOutputBackend {
public:
    virtual ~AnalysisOutputBackend() = default;

    virtual bool start(const VideoChannelStatus &status, QString *error) = 0;
    virtual bool stop(const QString &cameraId, QString *error) = 0;
    virtual AnalysisChannelStatus statusForCamera(const QString &cameraId) const = 0;
};

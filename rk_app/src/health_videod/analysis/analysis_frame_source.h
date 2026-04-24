#pragma once

#include "models/fall_models.h"

#include <QString>

class AnalysisFrameSource {
public:
    virtual ~AnalysisFrameSource() = default;

    virtual bool acceptsFrames(const QString &cameraId) const = 0;
    virtual void publishDescriptor(const AnalysisFrameDescriptor &descriptor) = 0;
};

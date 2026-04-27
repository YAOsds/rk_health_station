#pragma once

#include "models/fall_models.h"

#include <QString>

class AnalysisFrameSource {
public:
    virtual ~AnalysisFrameSource() = default;

    virtual bool acceptsFrames(const QString &cameraId) const = 0;
    virtual bool supportsDmaBufFrames() const { return false; }
    virtual void publishDescriptor(const AnalysisFrameDescriptor &descriptor) = 0;
    virtual void publishDmaBufDescriptor(const AnalysisFrameDescriptor &descriptor, int fd) {
        Q_UNUSED(descriptor);
        Q_UNUSED(fd);
    }
};

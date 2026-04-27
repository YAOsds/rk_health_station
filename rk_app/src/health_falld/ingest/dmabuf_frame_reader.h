#pragma once

#include "models/fall_models.h"

#include <QString>

class DmaBufFrameReader {
public:
    bool read(const AnalysisFrameDescriptor &descriptor, int fd,
        AnalysisFramePacket *packet, QString *error);
};

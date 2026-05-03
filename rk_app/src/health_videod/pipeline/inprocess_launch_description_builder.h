#pragma once

#include "pipeline/inprocess_gstreamer_pipeline.h"

#include <QString>

class InprocessLaunchDescriptionBuilder {
public:
    QString build(const InprocessGstreamerPipeline::Config &config) const;
};

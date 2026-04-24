#pragma once

#include "models/fall_models.h"

QByteArray encodeAnalysisFrameDescriptor(const AnalysisFrameDescriptor &descriptor);
bool decodeAnalysisFrameDescriptor(const QByteArray &bytes, AnalysisFrameDescriptor *descriptor);
bool takeFirstAnalysisFrameDescriptor(QByteArray *bytes, AnalysisFrameDescriptor *descriptor);

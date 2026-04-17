#pragma once

#include "models/fall_models.h"

QByteArray encodeAnalysisFramePacket(const AnalysisFramePacket &packet);
bool decodeAnalysisFramePacket(const QByteArray &bytes, AnalysisFramePacket *packet);
bool takeFirstAnalysisFramePacket(QByteArray *bytes, AnalysisFramePacket *packet);

#pragma once

#include "pose/pose_types.h"

#include <QVector>

class TargetSelector {
public:
    PosePerson selectPrimary(const QVector<PosePerson> &people) const;
};

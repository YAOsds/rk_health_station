#pragma once

#include "pose/pose_types.h"

#include <QString>
#include <QVector>

struct StgcnInputTensor {
    int channels = 3;
    int frames = 45;
    int joints = 17;
    QVector<float> values;
};

bool buildStgcnInputTensor(
    const QVector<PosePerson> &sequence, StgcnInputTensor *tensor, QString *error);

QVector<float> flattenSkeletonSequenceForLstm(const StgcnInputTensor &tensor);

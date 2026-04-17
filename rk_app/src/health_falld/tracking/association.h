#pragma once

#include <QRectF>
#include <QVector>

struct AssociationConfig {
    double matchThresh = 0.80;
    double maxCenterDistanceFactor = 1.5;
};

struct AssociationPair {
    int trackIndex = -1;
    int detectionIndex = -1;
    double score = 0.0;
};

double iou(const QRectF &left, const QRectF &right);
bool passesMotionGate(
    const QRectF &predicted, const QRectF &candidate, const AssociationConfig &config);
QVector<AssociationPair> greedyAssociate(const QVector<QRectF> &trackBoxes,
    const QVector<QRectF> &detectionBoxes, const AssociationConfig &config);

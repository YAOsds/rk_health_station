#pragma once

#include "pose/pose_types.h"

#include <QString>
#include <QVector>

struct ActionClassification {
    QString label = QStringLiteral("monitoring");
    double confidence = 0.0;
};

class ActionClassifier {
public:
    virtual ~ActionClassifier() = default;

    virtual bool loadModel(const QString &path, QString *error) = 0;
    virtual ActionClassification classify(const QVector<PosePerson> &sequence, QString *error) = 0;
};

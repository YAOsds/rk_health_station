#pragma once

#include <QString>
#include <QVector>

class RknnActionModelRunner {
public:
    ~RknnActionModelRunner();

    bool loadModel(const QString &path, QString *error);
    QVector<float> infer(const QVector<float> &input, QString *error);

private:
    QString modelPath_;
    void *runtime_ = nullptr;
};

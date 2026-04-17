#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

struct TelemetrySample {
    QString deviceId;
    qint64 sampleTime = 0;
    int heartRate = 0;
    double spo2 = 0.0;
    double acceleration = 0.0;
    bool fingerDetected = false;
    int battery = 0;
    int rssi = 0;
    QString wearState;
};

struct TelemetryBatch {
    QString deviceId;
    QVector<TelemetrySample> samples;
};

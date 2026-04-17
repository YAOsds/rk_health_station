#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;

class DeviceDetailPage : public QWidget {
    Q_OBJECT

public:
    explicit DeviceDetailPage(QWidget *parent = nullptr);

    void setDevice(const QJsonObject &device);

private:
    QLabel *deviceIdLabel_ = nullptr;
    QLabel *deviceNameLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *telemetryLabel_ = nullptr;
};

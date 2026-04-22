#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;

class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(QWidget *parent = nullptr);

    void setSnapshot(const QJsonObject &snapshot);
    QString wifiStatusText() const;
    QString wifiSsidText() const;
    QString wifiInterfaceText() const;
    QString wifiIpv4Text() const;

private:
    void setField(QLabel *label, const QString &name, const QString &value);

    QLabel *deviceNameLabel_ = nullptr;
    QLabel *onlineLabel_ = nullptr;
    QLabel *heartRateLabel_ = nullptr;
    QLabel *spo2Label_ = nullptr;
    QLabel *batteryLabel_ = nullptr;
    QLabel *updatedAtLabel_ = nullptr;
    QLabel *wifiStatusLabel_ = nullptr;
    QLabel *wifiSsidLabel_ = nullptr;
    QLabel *wifiInterfaceLabel_ = nullptr;
    QLabel *wifiIpv4Label_ = nullptr;
};

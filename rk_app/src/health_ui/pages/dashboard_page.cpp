#include "pages/dashboard_page.h"

#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>

DashboardPage::DashboardPage(QWidget *parent)
    : QWidget(parent)
    , deviceNameLabel_(new QLabel(QStringLiteral("--"), this))
    , onlineLabel_(new QLabel(QStringLiteral("--"), this))
    , heartRateLabel_(new QLabel(QStringLiteral("--"), this))
    , spo2Label_(new QLabel(QStringLiteral("--"), this))
    , batteryLabel_(new QLabel(QStringLiteral("--"), this))
    , updatedAtLabel_(new QLabel(QStringLiteral("--"), this))
    , wifiStatusLabel_(new QLabel(QStringLiteral("--"), this))
    , wifiSsidLabel_(new QLabel(QStringLiteral("--"), this))
    , wifiInterfaceLabel_(new QLabel(QStringLiteral("--"), this))
    , wifiIpv4Label_(new QLabel(QStringLiteral("--"), this)) {
    auto *layout = new QFormLayout(this);
    layout->addRow(QStringLiteral("device_name"), deviceNameLabel_);
    layout->addRow(QStringLiteral("online"), onlineLabel_);
    layout->addRow(QStringLiteral("heart_rate"), heartRateLabel_);
    layout->addRow(QStringLiteral("spo2"), spo2Label_);
    layout->addRow(QStringLiteral("battery"), batteryLabel_);
    layout->addRow(QStringLiteral("updated_at"), updatedAtLabel_);
    layout->addRow(QStringLiteral("status"), wifiStatusLabel_);
    layout->addRow(QStringLiteral("ssid"), wifiSsidLabel_);
    layout->addRow(QStringLiteral("interface"), wifiInterfaceLabel_);
    layout->addRow(QStringLiteral("ipv4 (for ESP)"), wifiIpv4Label_);
}

void DashboardPage::setSnapshot(const QJsonObject &snapshot) {
    QJsonObject currentDevice = snapshot.value(QStringLiteral("current_device")).toObject();
    if (currentDevice.isEmpty()) {
        const QJsonArray devices = snapshot.value(QStringLiteral("devices")).toArray();
        if (!devices.isEmpty()) {
            currentDevice = devices.first().toObject();
        }
    }

    setField(deviceNameLabel_, QStringLiteral("device_name"),
        currentDevice.value(QStringLiteral("device_name")).toString(QStringLiteral("--")));
    setField(onlineLabel_, QStringLiteral("online"),
        currentDevice.contains(QStringLiteral("online"))
            ? (currentDevice.value(QStringLiteral("online")).toBool() ? QStringLiteral("true")
                                                                      : QStringLiteral("false"))
            : QStringLiteral("--"));
    setField(heartRateLabel_, QStringLiteral("heart_rate"),
        currentDevice.contains(QStringLiteral("heart_rate"))
            ? QString::number(currentDevice.value(QStringLiteral("heart_rate")).toInt())
            : QStringLiteral("--"));
    setField(spo2Label_, QStringLiteral("spo2"),
        currentDevice.contains(QStringLiteral("spo2"))
            ? QString::number(currentDevice.value(QStringLiteral("spo2")).toDouble())
            : QStringLiteral("--"));
    setField(batteryLabel_, QStringLiteral("battery"),
        currentDevice.contains(QStringLiteral("battery"))
            ? QString::number(currentDevice.value(QStringLiteral("battery")).toInt())
            : QStringLiteral("--"));
    setField(updatedAtLabel_, QStringLiteral("updated_at"),
        currentDevice.contains(QStringLiteral("updated_at"))
            ? QString::number(static_cast<qint64>(
                  currentDevice.value(QStringLiteral("updated_at")).toDouble()))
            : (currentDevice.contains(QStringLiteral("last_seen_at"))
                      ? QString::number(static_cast<qint64>(
                            currentDevice.value(QStringLiteral("last_seen_at")).toDouble()))
                      : QStringLiteral("--")));

    const QJsonObject hostWifi = snapshot.value(QStringLiteral("host_wifi")).toObject();
    const bool present = hostWifi.value(QStringLiteral("present")).toBool(false);
    const bool connected = hostWifi.value(QStringLiteral("connected")).toBool(false);
    const QString wifiStatus = !present ? QStringLiteral("无 Wi-Fi 网卡")
        : (connected ? QStringLiteral("已连接") : QStringLiteral("未连接"));

    setField(wifiStatusLabel_, QStringLiteral("status"), wifiStatus);
    setField(wifiSsidLabel_, QStringLiteral("ssid"),
        hostWifi.value(QStringLiteral("ssid")).toString(QStringLiteral("--")));
    setField(wifiInterfaceLabel_, QStringLiteral("interface"),
        hostWifi.value(QStringLiteral("interface_name")).toString(QStringLiteral("--")));
    setField(wifiIpv4Label_, QStringLiteral("ipv4"),
        hostWifi.value(QStringLiteral("ipv4")).toString(QStringLiteral("--")));
}

void DashboardPage::setField(QLabel *label, const QString &, const QString &value) {
    if (label) {
        label->setText(value);
    }
}

QString DashboardPage::wifiStatusText() const {
    return wifiStatusLabel_ ? wifiStatusLabel_->text() : QString();
}

QString DashboardPage::wifiSsidText() const {
    return wifiSsidLabel_ ? wifiSsidLabel_->text() : QString();
}

QString DashboardPage::wifiInterfaceText() const {
    return wifiInterfaceLabel_ ? wifiInterfaceLabel_->text() : QString();
}

QString DashboardPage::wifiIpv4Text() const {
    return wifiIpv4Label_ ? wifiIpv4Label_->text() : QString();
}

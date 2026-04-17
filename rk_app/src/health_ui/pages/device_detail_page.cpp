#include "pages/device_detail_page.h"

#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>

DeviceDetailPage::DeviceDetailPage(QWidget *parent)
    : QWidget(parent)
    , deviceIdLabel_(new QLabel(QStringLiteral("--"), this))
    , deviceNameLabel_(new QLabel(QStringLiteral("--"), this))
    , statusLabel_(new QLabel(QStringLiteral("--"), this))
    , telemetryLabel_(new QLabel(QStringLiteral("--"), this)) {
    auto *layout = new QFormLayout(this);
    layout->addRow(QStringLiteral("device_id"), deviceIdLabel_);
    layout->addRow(QStringLiteral("device_name"), deviceNameLabel_);
    layout->addRow(QStringLiteral("status"), statusLabel_);
    layout->addRow(QStringLiteral("latest_summary"), telemetryLabel_);
}

void DeviceDetailPage::setDevice(const QJsonObject &device) {
    deviceIdLabel_->setText(device.value(QStringLiteral("device_id")).toString(QStringLiteral("--")));
    deviceNameLabel_->setText(device.value(QStringLiteral("device_name")).toString(QStringLiteral("--")));
    statusLabel_->setText(device.value(QStringLiteral("status")).toString(QStringLiteral("--")));
    telemetryLabel_->setText(QStringLiteral("online=%1, updated_at=%2")
                                 .arg(device.value(QStringLiteral("online")).toBool()
                                         ? QStringLiteral("true")
                                         : QStringLiteral("false"))
                                 .arg(QString::number(static_cast<qint64>(
                                     device.value(QStringLiteral("last_seen_at")).toDouble()))));
}

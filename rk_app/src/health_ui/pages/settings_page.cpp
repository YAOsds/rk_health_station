#include "pages/settings_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
    , deviceSelector_(new QComboBox(this))
    , deviceNameEdit_(new QLineEdit(this))
    , enabledCheckBox_(new QCheckBox(QStringLiteral("Enabled"), this))
    , secretEdit_(new QLineEdit(this)) {
    auto *renameButton = new QPushButton(QStringLiteral("Rename"), this);
    auto *applyEnabledButton = new QPushButton(QStringLiteral("Apply Enabled"), this);
    auto *resetSecretButton = new QPushButton(QStringLiteral("Reset Secret"), this);

    auto *formLayout = new QFormLayout();
    formLayout->addRow(QStringLiteral("device"), deviceSelector_);
    formLayout->addRow(QStringLiteral("device_name"), deviceNameEdit_);
    formLayout->addRow(QStringLiteral("enabled"), enabledCheckBox_);
    formLayout->addRow(QStringLiteral("secret_hash"), secretEdit_);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(renameButton);
    buttonLayout->addWidget(applyEnabledButton);
    buttonLayout->addWidget(resetSecretButton);
    buttonLayout->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addLayout(buttonLayout);
    layout->addStretch(1);

    connect(renameButton, &QPushButton::clicked, this, &SettingsPage::onRenameClicked);
    connect(applyEnabledButton, &QPushButton::clicked, this, &SettingsPage::onSetEnabledClicked);
    connect(resetSecretButton, &QPushButton::clicked, this, &SettingsPage::onResetSecretClicked);
}

void SettingsPage::setDevices(const QJsonArray &devices) {
    const QString previousSelection = selectedDeviceId();
    deviceSelector_->clear();

    for (const QJsonValue &value : devices) {
        const QJsonObject object = value.toObject();
        const QString deviceId = object.value(QStringLiteral("device_id")).toString();
        const QString deviceName = object.value(QStringLiteral("device_name")).toString();
        deviceSelector_->addItem(
            QStringLiteral("%1 (%2)").arg(deviceName, deviceId), deviceId);
    }

    const int index = previousSelection.isEmpty()
        ? (deviceSelector_->count() > 0 ? 0 : -1)
        : deviceSelector_->findData(previousSelection);
    if (index >= 0) {
        deviceSelector_->setCurrentIndex(index);
    }
}

void SettingsPage::onRenameClicked() {
    const QString deviceId = selectedDeviceId();
    const QString name = deviceNameEdit_->text().trimmed();
    if (deviceId.isEmpty() || name.isEmpty()) {
        return;
    }
    emit renameRequested(deviceId, name);
}

void SettingsPage::onSetEnabledClicked() {
    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty()) {
        return;
    }
    emit setDeviceEnabledRequested(deviceId, enabledCheckBox_->isChecked());
}

void SettingsPage::onResetSecretClicked() {
    const QString deviceId = selectedDeviceId();
    const QString secretHash = secretEdit_->text().trimmed();
    if (deviceId.isEmpty() || secretHash.isEmpty()) {
        return;
    }
    emit resetSecretRequested(deviceId, secretHash);
}

QString SettingsPage::selectedDeviceId() const {
    return deviceSelector_->currentData().toString();
}

#include "pages/device_approval_page.h"

#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

DeviceApprovalPage::DeviceApprovalPage(QWidget *parent)
    : QWidget(parent)
    , table_(new QTableWidget(this))
    , approvedNameEdit_(new QLineEdit(this))
    , secretEdit_(new QLineEdit(this)) {
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("device_id"), QStringLiteral("proposed_name"),
            QStringLiteral("firmware_version"), QStringLiteral("hardware_model"),
            QStringLiteral("ip"), QStringLiteral("request_time")});
    table_->horizontalHeader()->setStretchLastSection(true);

    auto *approveButton = new QPushButton(QStringLiteral("Approve"), this);
    auto *rejectButton = new QPushButton(QStringLiteral("Reject"), this);

    auto *formLayout = new QFormLayout();
    formLayout->addRow(QStringLiteral("approved_name"), approvedNameEdit_);
    formLayout->addRow(QStringLiteral("secret_hash"), secretEdit_);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(approveButton);
    buttonLayout->addWidget(rejectButton);
    buttonLayout->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(table_);
    layout->addLayout(formLayout);
    layout->addLayout(buttonLayout);

    connect(approveButton, &QPushButton::clicked, this, &DeviceApprovalPage::onApproveClicked);
    connect(rejectButton, &QPushButton::clicked, this, &DeviceApprovalPage::onRejectClicked);
}

void DeviceApprovalPage::setPendingDevices(const QJsonArray &devices) {
    table_->setRowCount(devices.size());
    for (int row = 0; row < devices.size(); ++row) {
        const QJsonObject object = devices.at(row).toObject();
        table_->setItem(row, 0, new QTableWidgetItem(
            object.value(QStringLiteral("device_id")).toString()));
        table_->setItem(row, 1, new QTableWidgetItem(
            object.value(QStringLiteral("proposed_name")).toString()));
        table_->setItem(row, 2, new QTableWidgetItem(
            object.value(QStringLiteral("firmware_version")).toString()));
        table_->setItem(row, 3, new QTableWidgetItem(
            object.value(QStringLiteral("hardware_model")).toString()));
        table_->setItem(row, 4, new QTableWidgetItem(
            object.value(QStringLiteral("ip")).toString()));
        table_->setItem(row, 5, new QTableWidgetItem(QString::number(
            static_cast<qint64>(object.value(QStringLiteral("request_time")).toDouble()))));
    }

    if (devices.isEmpty()) {
        approvedNameEdit_->clear();
        return;
    }

    table_->selectRow(0);
    approvedNameEdit_->setText(devices.first().toObject().value(QStringLiteral("proposed_name")).toString());
}

void DeviceApprovalPage::onApproveClicked() {
    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty()) {
        return;
    }

    QString approvedName = approvedNameEdit_->text().trimmed();
    if (approvedName.isEmpty()) {
        approvedName = selectedProposedName();
    }
    emit approveRequested(deviceId, approvedName, secretEdit_->text().trimmed());
}

void DeviceApprovalPage::onRejectClicked() {
    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty()) {
        return;
    }
    emit rejectRequested(deviceId);
}

QString DeviceApprovalPage::selectedDeviceId() const {
    const int row = table_->currentRow();
    if (row < 0 || !table_->item(row, 0)) {
        return {};
    }
    return table_->item(row, 0)->text();
}

QString DeviceApprovalPage::selectedProposedName() const {
    const int row = table_->currentRow();
    if (row < 0 || !table_->item(row, 1)) {
        return {};
    }
    return table_->item(row, 1)->text();
}

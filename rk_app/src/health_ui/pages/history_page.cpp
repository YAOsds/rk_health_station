#include "pages/history_page.h"

#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QJsonValue>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

HistoryPage::HistoryPage(QWidget *parent)
    : QWidget(parent)
    , deviceSelector_(new QComboBox(this))
    , fromEdit_(new QLineEdit(this))
    , toEdit_(new QLineEdit(this))
    , table_(new QTableWidget(this)) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    fromEdit_->setText(QString::number(now - 3600));
    toEdit_->setText(QString::number(now));

    auto *refreshButton = new QPushButton(QStringLiteral("Refresh"), this);

    auto *formLayout = new QFormLayout();
    formLayout->addRow(QStringLiteral("device"), deviceSelector_);
    formLayout->addRow(QStringLiteral("from_ts"), fromEdit_);
    formLayout->addRow(QStringLiteral("to_ts"), toEdit_);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(refreshButton);
    buttonLayout->addStretch(1);

    table_->setColumnCount(9);
    table_->setHorizontalHeaderLabels({QStringLiteral("minute_ts"), QStringLiteral("samples_total"),
        QStringLiteral("avg_heart_rate"), QStringLiteral("min_heart_rate"),
        QStringLiteral("max_heart_rate"), QStringLiteral("avg_spo2"),
        QStringLiteral("min_spo2"), QStringLiteral("max_spo2"), QStringLiteral("avg_battery")});
    table_->horizontalHeader()->setStretchLastSection(true);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addLayout(buttonLayout);
    layout->addWidget(table_);

    connect(refreshButton, &QPushButton::clicked, this, &HistoryPage::onRefreshClicked);
}

void HistoryPage::setDevices(const QJsonArray &devices) {
    const QString previousSelection = selectedDeviceId();
    deviceSelector_->clear();

    for (const QJsonValue &value : devices) {
        const QJsonObject object = value.toObject();
        const QString deviceId = object.value(QStringLiteral("device_id")).toString();
        const QString deviceName = object.value(QStringLiteral("device_name")).toString();
        deviceSelector_->addItem(QStringLiteral("%1 (%2)").arg(deviceName, deviceId), deviceId);
    }

    const int index = previousSelection.isEmpty()
        ? (deviceSelector_->count() > 0 ? 0 : -1)
        : deviceSelector_->findData(previousSelection);
    if (index >= 0) {
        deviceSelector_->setCurrentIndex(index);
    }
}

void HistoryPage::setSeries(const QJsonObject &payload) {
    const QJsonArray series = payload.value(QStringLiteral("series")).toArray();
    table_->setRowCount(series.size());
    for (int row = 0; row < series.size(); ++row) {
        const QJsonObject object = series.at(row).toObject();
        table_->setItem(row, 0, new QTableWidgetItem(QString::number(
            static_cast<qint64>(object.value(QStringLiteral("minute_ts")).toDouble()))));
        table_->setItem(row, 1, new QTableWidgetItem(
            QString::number(object.value(QStringLiteral("samples_total")).toInt())));
        table_->setItem(row, 2, new QTableWidgetItem(
            object.contains(QStringLiteral("avg_heart_rate"))
                ? QString::number(object.value(QStringLiteral("avg_heart_rate")).toDouble())
                : QStringLiteral("--")));
        table_->setItem(row, 3, new QTableWidgetItem(
            object.contains(QStringLiteral("min_heart_rate"))
                ? QString::number(object.value(QStringLiteral("min_heart_rate")).toInt())
                : QStringLiteral("--")));
        table_->setItem(row, 4, new QTableWidgetItem(
            object.contains(QStringLiteral("max_heart_rate"))
                ? QString::number(object.value(QStringLiteral("max_heart_rate")).toInt())
                : QStringLiteral("--")));
        table_->setItem(row, 5, new QTableWidgetItem(
            object.contains(QStringLiteral("avg_spo2"))
                ? QString::number(object.value(QStringLiteral("avg_spo2")).toDouble())
                : QStringLiteral("--")));
        table_->setItem(row, 6, new QTableWidgetItem(
            object.contains(QStringLiteral("min_spo2"))
                ? QString::number(object.value(QStringLiteral("min_spo2")).toDouble())
                : QStringLiteral("--")));
        table_->setItem(row, 7, new QTableWidgetItem(
            object.contains(QStringLiteral("max_spo2"))
                ? QString::number(object.value(QStringLiteral("max_spo2")).toDouble())
                : QStringLiteral("--")));
        table_->setItem(row, 8, new QTableWidgetItem(
            object.contains(QStringLiteral("avg_battery"))
                ? QString::number(object.value(QStringLiteral("avg_battery")).toDouble())
                : QStringLiteral("--")));
    }
}

void HistoryPage::requestRefresh() {
    onRefreshClicked();
}

void HistoryPage::onRefreshClicked() {
    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty()) {
        return;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    emit historyRequested(deviceId, parseTimestamp(fromEdit_, now - 3600), parseTimestamp(toEdit_, now));
}

QString HistoryPage::selectedDeviceId() const {
    return deviceSelector_->currentData().toString();
}

qint64 HistoryPage::parseTimestamp(const QLineEdit *edit, qint64 fallback) const {
    if (!edit) {
        return fallback;
    }

    bool ok = false;
    const qint64 value = edit->text().trimmed().toLongLong(&ok);
    return ok ? value : fallback;
}

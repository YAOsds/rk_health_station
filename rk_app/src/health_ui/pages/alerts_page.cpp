#include "pages/alerts_page.h"

#include <QHeaderView>
#include <QJsonObject>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

AlertsPage::AlertsPage(QWidget *parent)
    : QWidget(parent)
    , table_(new QTableWidget(this)) {
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({QStringLiteral("device_name"), QStringLiteral("device_id"),
        QStringLiteral("alert_id"), QStringLiteral("severity"), QStringLiteral("since"),
        QStringLiteral("message")});
    table_->horizontalHeader()->setStretchLastSection(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(table_);
}

void AlertsPage::setAlerts(const QJsonArray &alerts) {
    table_->setRowCount(alerts.size());
    for (int row = 0; row < alerts.size(); ++row) {
        const QJsonObject object = alerts.at(row).toObject();
        table_->setItem(row, 0, new QTableWidgetItem(
            object.value(QStringLiteral("device_name")).toString()));
        table_->setItem(row, 1, new QTableWidgetItem(
            object.value(QStringLiteral("device_id")).toString()));
        table_->setItem(row, 2, new QTableWidgetItem(
            object.value(QStringLiteral("alert_id")).toString()));
        table_->setItem(row, 3, new QTableWidgetItem(
            object.value(QStringLiteral("severity")).toString()));
        table_->setItem(row, 4, new QTableWidgetItem(QString::number(
            static_cast<qint64>(object.value(QStringLiteral("since")).toDouble()))));
        table_->setItem(row, 5, new QTableWidgetItem(
            object.value(QStringLiteral("message")).toString()));
    }
}

#include "pages/device_list_page.h"

#include <QHeaderView>
#include <QJsonObject>
#include <QTableWidget>
#include <QVBoxLayout>

DeviceListPage::DeviceListPage(QWidget *parent)
    : QWidget(parent)
    , table_(new QTableWidget(this)) {
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("device_name"), QStringLiteral("device_id"), QStringLiteral("online"),
            QStringLiteral("status")});
    table_->horizontalHeader()->setStretchLastSection(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(table_);
}

void DeviceListPage::setDevices(const QJsonArray &devices) {
    table_->setRowCount(devices.size());
    for (int row = 0; row < devices.size(); ++row) {
        const QJsonObject object = devices.at(row).toObject();
        table_->setItem(row, 0, new QTableWidgetItem(
            object.value(QStringLiteral("device_name")).toString()));
        table_->setItem(row, 1, new QTableWidgetItem(
            object.value(QStringLiteral("device_id")).toString()));
        table_->setItem(row, 2, new QTableWidgetItem(
            object.value(QStringLiteral("online")).toBool() ? QStringLiteral("true")
                                                            : QStringLiteral("false")));
        table_->setItem(row, 3, new QTableWidgetItem(
            object.value(QStringLiteral("status")).toString()));
    }
}

#pragma once

#include <QJsonArray>
#include <QWidget>

class QTableWidget;

class DeviceListPage : public QWidget {
    Q_OBJECT

public:
    explicit DeviceListPage(QWidget *parent = nullptr);

    void setDevices(const QJsonArray &devices);

private:
    QTableWidget *table_ = nullptr;
};

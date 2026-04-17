#pragma once

#include <QJsonArray>
#include <QWidget>

class QTableWidget;

class AlertsPage : public QWidget {
    Q_OBJECT

public:
    explicit AlertsPage(QWidget *parent = nullptr);

    void setAlerts(const QJsonArray &alerts);

private:
    QTableWidget *table_ = nullptr;
};

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QTableWidget;

class HistoryPage : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPage(QWidget *parent = nullptr);

    void setDevices(const QJsonArray &devices);
    void setSeries(const QJsonObject &payload);
    void requestRefresh();

signals:
    void historyRequested(const QString &deviceId, qint64 fromTs, qint64 toTs);

private slots:
    void onRefreshClicked();

private:
    QString selectedDeviceId() const;
    qint64 parseTimestamp(const QLineEdit *edit, qint64 fallback) const;

    QComboBox *deviceSelector_ = nullptr;
    QLineEdit *fromEdit_ = nullptr;
    QLineEdit *toEdit_ = nullptr;
    QTableWidget *table_ = nullptr;
};

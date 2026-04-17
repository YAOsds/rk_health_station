#pragma once

#include <QJsonArray>
#include <QWidget>

class QLineEdit;
class QTableWidget;

class DeviceApprovalPage : public QWidget {
    Q_OBJECT

public:
    explicit DeviceApprovalPage(QWidget *parent = nullptr);

    void setPendingDevices(const QJsonArray &devices);

signals:
    void approveRequested(
        const QString &deviceId, const QString &deviceName, const QString &secretHash);
    void rejectRequested(const QString &deviceId);

private slots:
    void onApproveClicked();
    void onRejectClicked();

private:
    QString selectedDeviceId() const;
    QString selectedProposedName() const;

    QTableWidget *table_ = nullptr;
    QLineEdit *approvedNameEdit_ = nullptr;
    QLineEdit *secretEdit_ = nullptr;
};

#pragma once

#include <QJsonArray>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

    void setDevices(const QJsonArray &devices);

signals:
    void renameRequested(const QString &deviceId, const QString &deviceName);
    void setDeviceEnabledRequested(const QString &deviceId, bool enabled);
    void resetSecretRequested(const QString &deviceId, const QString &secretHash);

private slots:
    void onRenameClicked();
    void onSetEnabledClicked();
    void onResetSecretClicked();

private:
    QString selectedDeviceId() const;

    QComboBox *deviceSelector_ = nullptr;
    QLineEdit *deviceNameEdit_ = nullptr;
    QCheckBox *enabledCheckBox_ = nullptr;
    QLineEdit *secretEdit_ = nullptr;
};

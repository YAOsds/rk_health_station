#pragma once

#include <QObject>

class AlertsPage;
class DashboardPage;
class DeviceDetailPage;
class DeviceListPage;
class DeviceApprovalPage;
class HistoryPage;
class QMainWindow;
class QPushButton;
class QStackedWidget;
class QTimer;
class SettingsPage;
class UiIpcClient;
class FallIpcClient;
class VideoIpcClient;
class VideoMonitorPage;
class QJsonArray;
class QJsonObject;

class UiApp : public QObject {
    Q_OBJECT

public:
    explicit UiApp(QObject *parent = nullptr);

    bool start();
    QMainWindow *window() const;
    void openVideoPage();

private slots:
    void onDeviceListReceived(const QJsonArray &devices);
    void onDashboardSnapshotReceived(const QJsonObject &snapshot);
    void onPendingDevicesReceived(const QJsonArray &devices);
    void onAlertsSnapshotReceived(const QJsonArray &alerts);
    void onHistorySeriesReceived(const QJsonObject &payload);
    void onOperationFinished(const QString &action, bool ok, const QJsonObject &payload);

private:
    UiIpcClient *client_ = nullptr;
    QMainWindow *window_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    DashboardPage *dashboardPage_ = nullptr;
    DeviceListPage *deviceListPage_ = nullptr;
    DeviceDetailPage *deviceDetailPage_ = nullptr;
    DeviceApprovalPage *deviceApprovalPage_ = nullptr;
    SettingsPage *settingsPage_ = nullptr;
    AlertsPage *alertsPage_ = nullptr;
    HistoryPage *historyPage_ = nullptr;
    VideoIpcClient *videoClient_ = nullptr;
    FallIpcClient *fallClient_ = nullptr;
    VideoMonitorPage *videoMonitorPage_ = nullptr;
    QTimer *dashboardRefreshTimer_ = nullptr;
};

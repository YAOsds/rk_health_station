#include "app/ui_app.h"

#include "ipc_client/fall_ipc_client.h"
#include "ipc_client/ui_ipc_client.h"
#include "ipc_client/video_ipc_client.h"
#include "pages/alerts_page.h"
#include "pages/dashboard_page.h"
#include "pages/device_approval_page.h"
#include "pages/device_detail_page.h"
#include "pages/device_list_page.h"
#include "pages/history_page.h"
#include "pages/video_monitor_page.h"
#include "pages/settings_page.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

UiApp::UiApp(QObject *parent)
    : QObject(parent)
    , client_(new UiIpcClient(this))
    , window_(new QMainWindow())
    , stack_(new QStackedWidget(window_))
    , dashboardPage_(new DashboardPage(stack_))
    , deviceListPage_(new DeviceListPage(stack_))
    , deviceDetailPage_(new DeviceDetailPage(stack_))
    , deviceApprovalPage_(new DeviceApprovalPage(stack_))
    , settingsPage_(new SettingsPage(stack_))
    , alertsPage_(new AlertsPage(stack_))
    , historyPage_(new HistoryPage(stack_))
    , videoClient_(new VideoIpcClient(this))
    , fallClient_(new FallIpcClient(QString(), this))
    , videoMonitorPage_(new VideoMonitorPage(videoClient_, fallClient_, stack_)) {
    auto *central = new QWidget(window_);
    auto *rootLayout = new QVBoxLayout(central);
    auto *navLayout = new QHBoxLayout();
    auto *dashboardButton = new QPushButton(QStringLiteral("Dashboard"), central);
    auto *devicesButton = new QPushButton(QStringLiteral("Devices"), central);
    auto *detailButton = new QPushButton(QStringLiteral("Device Detail"), central);
    auto *approvalButton = new QPushButton(QStringLiteral("Approval"), central);
    auto *settingsButton = new QPushButton(QStringLiteral("Settings"), central);
    auto *alertsButton = new QPushButton(QStringLiteral("Alerts"), central);
    auto *historyButton = new QPushButton(QStringLiteral("History"), central);
    auto *videoButton = new QPushButton(QStringLiteral("Video"), central);

    navLayout->addWidget(dashboardButton);
    navLayout->addWidget(devicesButton);
    navLayout->addWidget(detailButton);
    navLayout->addWidget(approvalButton);
    navLayout->addWidget(settingsButton);
    navLayout->addWidget(alertsButton);
    navLayout->addWidget(historyButton);
    navLayout->addWidget(videoButton);
    navLayout->addStretch(1);

    stack_->addWidget(dashboardPage_);
    stack_->addWidget(deviceListPage_);
    stack_->addWidget(deviceDetailPage_);
    stack_->addWidget(deviceApprovalPage_);
    stack_->addWidget(settingsPage_);
    stack_->addWidget(alertsPage_);
    stack_->addWidget(historyPage_);
    stack_->addWidget(videoMonitorPage_);

    rootLayout->addLayout(navLayout);
    rootLayout->addWidget(stack_);
    window_->setCentralWidget(central);
    window_->resize(900, 600);
    window_->setWindowTitle(QStringLiteral("RK Health UI"));

    connect(dashboardButton, &QPushButton::clicked, this, [this]() {
        stack_->setCurrentWidget(dashboardPage_);
    });
    connect(devicesButton, &QPushButton::clicked, this, [this]() {
        stack_->setCurrentWidget(deviceListPage_);
    });
    connect(detailButton, &QPushButton::clicked, this, [this]() {
        stack_->setCurrentWidget(deviceDetailPage_);
    });
    connect(approvalButton, &QPushButton::clicked, this, [this]() {
        stack_->setCurrentWidget(deviceApprovalPage_);
        client_->requestPendingDevices();
    });
    connect(settingsButton, &QPushButton::clicked, this, [this]() {
        stack_->setCurrentWidget(settingsPage_);
    });
    connect(alertsButton, &QPushButton::clicked, this, [this]() {
        stack_->setCurrentWidget(alertsPage_);
        client_->requestAlertsSnapshot();
    });
    connect(historyButton, &QPushButton::clicked, this, [this]() {
        stack_->setCurrentWidget(historyPage_);
        historyPage_->requestRefresh();
    });
    connect(videoButton, &QPushButton::clicked, this, [this]() {
        openVideoPage();
    });

    connect(client_, &UiIpcClient::deviceListReceived, this, &UiApp::onDeviceListReceived);
    connect(client_, &UiIpcClient::dashboardSnapshotReceived, this,
        &UiApp::onDashboardSnapshotReceived);
    connect(client_, &UiIpcClient::pendingDevicesReceived, this, &UiApp::onPendingDevicesReceived);
    connect(client_, &UiIpcClient::alertsSnapshotReceived, this, &UiApp::onAlertsSnapshotReceived);
    connect(client_, &UiIpcClient::historySeriesReceived, this, &UiApp::onHistorySeriesReceived);
    connect(client_, &UiIpcClient::operationFinished, this, &UiApp::onOperationFinished);

    connect(deviceApprovalPage_, &DeviceApprovalPage::approveRequested, this,
        [this](const QString &deviceId, const QString &deviceName, const QString &secretHash) {
            client_->approveDevice(deviceId, deviceName, secretHash);
        });
    connect(deviceApprovalPage_, &DeviceApprovalPage::rejectRequested, this,
        [this](const QString &deviceId) {
            client_->rejectDevice(deviceId);
        });
    connect(settingsPage_, &SettingsPage::renameRequested, this,
        [this](const QString &deviceId, const QString &deviceName) {
            client_->renameDevice(deviceId, deviceName);
        });
    connect(settingsPage_, &SettingsPage::setDeviceEnabledRequested, this,
        [this](const QString &deviceId, bool enabled) {
            client_->setDeviceEnabled(deviceId, enabled);
        });
    connect(settingsPage_, &SettingsPage::resetSecretRequested, this,
        [this](const QString &deviceId, const QString &secretHash) {
            client_->resetDeviceSecret(deviceId, secretHash);
        });
    connect(historyPage_, &HistoryPage::historyRequested, this,
        [this](const QString &deviceId, qint64 fromTs, qint64 toTs) {
            client_->requestHistorySeries(deviceId, fromTs, toTs);
        });
}

bool UiApp::start() {
    qInfo() << "health-ui lifecycle: showing main window";
    window_->show();

    const bool connected = client_->connectToBackend();
    qInfo() << "health-ui lifecycle: backend connection result" << connected;
    if (connected) {
        qInfo() << "health-ui lifecycle: dispatching initial requests";
        client_->requestDeviceList();
        client_->requestDashboardSnapshot();
        client_->requestPendingDevices();
        client_->requestAlertsSnapshot();
    }
    return connected;
}

QMainWindow *UiApp::window() const {
    return window_;
}

void UiApp::openVideoPage() {
    qInfo() << "health-ui lifecycle: switching to video page";
    stack_->setCurrentWidget(videoMonitorPage_);
    const bool connected = videoClient_->connectToBackend();
    const bool fallConnected = fallClient_->connectToBackend();
    qInfo() << "health-ui lifecycle: video backend connection result" << connected;
    qInfo() << "health-ui lifecycle: fall backend connection result" << fallConnected;
    videoClient_->requestStatus(QStringLiteral("front_cam"));
}

void UiApp::onDeviceListReceived(const QJsonArray &devices) {
    qInfo() << "health-ui ui: device list received"
            << "device_count=" << devices.size();
    deviceListPage_->setDevices(devices);
    settingsPage_->setDevices(devices);
    historyPage_->setDevices(devices);
    if (!devices.isEmpty()) {
        deviceDetailPage_->setDevice(devices.first().toObject());
    }
}

void UiApp::onDashboardSnapshotReceived(const QJsonObject &snapshot) {
    qInfo() << "health-ui ui: dashboard snapshot received"
            << "keys=" << snapshot.keys();
    dashboardPage_->setSnapshot(snapshot);
}

void UiApp::onPendingDevicesReceived(const QJsonArray &devices) {
    qInfo() << "health-ui ui: pending devices received"
            << "pending_count=" << devices.size();
    deviceApprovalPage_->setPendingDevices(devices);
}

void UiApp::onAlertsSnapshotReceived(const QJsonArray &alerts) {
    qInfo() << "health-ui ui: alerts snapshot received"
            << "alert_count=" << alerts.size();
    alertsPage_->setAlerts(alerts);
}

void UiApp::onHistorySeriesReceived(const QJsonObject &payload) {
    qInfo() << "health-ui ui: history series received"
            << "keys=" << payload.keys();
    historyPage_->setSeries(payload);
}

void UiApp::onOperationFinished(const QString &, bool ok, const QJsonObject &) {
    qInfo() << "health-ui ui: operation finished"
            << "ok=" << ok;
    if (!ok) {
        return;
    }

    qInfo() << "health-ui lifecycle: refreshing views after successful operation";
    client_->requestDeviceList();
    client_->requestDashboardSnapshot();
    client_->requestPendingDevices();
    client_->requestAlertsSnapshot();
}

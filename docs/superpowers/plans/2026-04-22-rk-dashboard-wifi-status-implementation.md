# RK3588 Dashboard Wi-Fi Status Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a host-side Wi-Fi status card to the RK3588 `Dashboard` page that shows connection status, SSID, Wi-Fi interface name, and the current IPv4 address the ESP should use as its target IP.

**Architecture:** `healthd` remains the single source of truth. A small host Wi-Fi provider probes NetworkManager via `nmcli`, falls back to `QNetworkInterface` for interface/IP presence, and injects a `host_wifi` object into the existing dashboard snapshot. `health-ui` renders that object in a dedicated Wi-Fi section on `DashboardPage` and refreshes the dashboard snapshot every 3 seconds.

**Tech Stack:** Qt 5/6 Core, Network, Widgets, `QProcess`, `QNetworkInterface`, local-socket IPC, Qt Test / CTest, existing `healthd` + `health-ui` code paths.

---

## File Structure Map

### Backend production files
- Create: `rk_app/src/healthd/host/host_wifi_status.h` - shared value object for RK3588 host Wi-Fi state.
- Create: `rk_app/src/healthd/host/host_wifi_status_provider.h` - provider interface, default `nmcli` implementation, and testable probe entrypoints.
- Create: `rk_app/src/healthd/host/host_wifi_status_provider.cpp` - `nmcli` probing, fallback interface/IP detection, and JSON helpers.
- Modify: `rk_app/src/healthd/CMakeLists.txt` - compile the new host Wi-Fi provider into `healthd`.
- Modify: `rk_app/src/healthd/app/daemon_app.h` - store the default host Wi-Fi provider instance.
- Modify: `rk_app/src/healthd/app/daemon_app.cpp` - pass the provider into `UiGateway`.
- Modify: `rk_app/src/healthd/ipc_server/ui_gateway.h` - accept a host Wi-Fi provider dependency and add a JSON builder helper.
- Modify: `rk_app/src/healthd/ipc_server/ui_gateway.cpp` - attach `host_wifi` to dashboard snapshot responses.

### UI production files
- Modify: `rk_app/src/health_ui/pages/dashboard_page.h` - add labels and test accessors for Wi-Fi rows.
- Modify: `rk_app/src/health_ui/pages/dashboard_page.cpp` - render the new `host_wifi` card.
- Modify: `rk_app/src/health_ui/app/ui_app.h` - add a dashboard refresh timer member.
- Modify: `rk_app/src/health_ui/app/ui_app.cpp` - start a 3-second dashboard refresh loop after backend connection.

### Test files
- Create: `rk_app/src/tests/host_tests/host_wifi_status_provider_test.cpp` - parser and fallback coverage for the host Wi-Fi provider.
- Modify: `rk_app/src/tests/ipc_tests/ui_gateway_test.cpp` - assert `host_wifi` is present in dashboard snapshot responses.
- Create: `rk_app/src/tests/ui_tests/dashboard_page_test.cpp` - verify UI rendering for connected, disconnected, and no-NIC states.
- Create: `rk_app/src/tests/ui_tests/ui_app_dashboard_refresh_test.cpp` - verify repeated `get_dashboard_snapshot` requests every 3 seconds.
- Modify: `rk_app/src/tests/CMakeLists.txt` - register the new test executables and add provider sources to test targets.

---

### Task 1: Build a testable host Wi-Fi provider with `nmcli` parsing and fallback detection

**Files:**
- Create: `rk_app/src/healthd/host/host_wifi_status.h`
- Create: `rk_app/src/healthd/host/host_wifi_status_provider.h`
- Create: `rk_app/src/healthd/host/host_wifi_status_provider.cpp`
- Create: `rk_app/src/tests/host_tests/host_wifi_status_provider_test.cpp`
- Modify: `rk_app/src/healthd/CMakeLists.txt`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing provider tests**

```cpp
#include "host/host_wifi_status_provider.h"

#include <QtTest/QTest>

class HostWifiStatusProviderTest : public QObject {
    Q_OBJECT

private slots:
    void parsesConnectedNmcliRows();
    void fallsBackWhenNmcliOutputIsMalformed();
    void reportsDisconnectedWifiNicFromFallback();
    void reportsNoWifiNicWhenFallbackIsEmpty();
};

void HostWifiStatusProviderTest::parsesConnectedNmcliRows() {
    const QByteArray deviceOutput =
        "yes:wifi:wlan0:Office_AP\n"
        "no:ethernet:eth0:\n";
    const QList<QHostAddress> addrs {QHostAddress(QStringLiteral("192.168.137.23"))};

    const HostWifiStatus status
        = NmcliHostWifiStatusProvider::statusFromProbeOutputs(deviceOutput, QStringLiteral("wlan0"), addrs);

    QVERIFY(status.present);
    QVERIFY(status.connected);
    QCOMPARE(status.interfaceName, QStringLiteral("wlan0"));
    QCOMPARE(status.ssid, QStringLiteral("Office_AP"));
    QCOMPARE(status.ipv4, QStringLiteral("192.168.137.23"));
}

void HostWifiStatusProviderTest::fallsBackWhenNmcliOutputIsMalformed() {
    const HostWifiStatus status = NmcliHostWifiStatusProvider::statusFromProbeOutputs(
        QByteArray("not-a-valid-row"), QStringLiteral("wlan0"), QList<QHostAddress>());

    QVERIFY(status.present);
    QVERIFY(!status.connected);
    QCOMPARE(status.interfaceName, QStringLiteral("wlan0"));
    QCOMPARE(status.ssid, QStringLiteral("--"));
}

void HostWifiStatusProviderTest::reportsDisconnectedWifiNicFromFallback() {
    const HostWifiStatus status = NmcliHostWifiStatusProvider::fallbackStatus(
        QStringLiteral("wlan0"), QList<QHostAddress>());

    QVERIFY(status.present);
    QVERIFY(!status.connected);
    QCOMPARE(status.interfaceName, QStringLiteral("wlan0"));
    QCOMPARE(status.ssid, QStringLiteral("--"));
    QCOMPARE(status.ipv4, QStringLiteral("--"));
}

void HostWifiStatusProviderTest::reportsNoWifiNicWhenFallbackIsEmpty() {
    const HostWifiStatus status = NmcliHostWifiStatusProvider::fallbackStatus(
        QString(), QList<QHostAddress>());

    QVERIFY(!status.present);
    QVERIFY(!status.connected);
    QCOMPARE(status.interfaceName, QStringLiteral("--"));
    QCOMPARE(status.ssid, QStringLiteral("--"));
    QCOMPARE(status.ipv4, QStringLiteral("--"));
}

QTEST_MAIN(HostWifiStatusProviderTest)
#include "host_wifi_status_provider_test.moc"
```

- [ ] **Step 2: Run the new test target and verify it fails**

Run: `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON && ctest --test-dir out/build-rk_app-host -R host_wifi_status_provider_test --output-on-failure`

Expected: configure or build fails because `host_tests/host_wifi_status_provider_test.cpp` and the new provider files are not registered yet.

- [ ] **Step 3: Write the minimal provider implementation**

```cpp
// rk_app/src/healthd/host/host_wifi_status.h
#pragma once

#include <QString>

struct HostWifiStatus {
    bool present = false;
    bool connected = false;
    QString interfaceName = QStringLiteral("--");
    QString ssid = QStringLiteral("--");
    QString ipv4 = QStringLiteral("--");
};

// rk_app/src/healthd/host/host_wifi_status_provider.h
#pragma once

#include "host/host_wifi_status.h"

#include <QHostAddress>
#include <QList>
#include <QObject>

class HostWifiStatusProvider : public QObject {
    Q_OBJECT

public:
    explicit HostWifiStatusProvider(QObject *parent = nullptr);
    ~HostWifiStatusProvider() override = default;

    virtual HostWifiStatus currentStatus() const = 0;
};

class NmcliHostWifiStatusProvider final : public HostWifiStatusProvider {
    Q_OBJECT

public:
    explicit NmcliHostWifiStatusProvider(QObject *parent = nullptr);

    HostWifiStatus currentStatus() const override;

    static HostWifiStatus statusFromProbeOutputs(
        const QByteArray &nmcliDeviceOutput, const QString &fallbackInterfaceName,
        const QList<QHostAddress> &fallbackIpv4Addresses);
    static HostWifiStatus fallbackStatus(
        const QString &fallbackInterfaceName, const QList<QHostAddress> &fallbackIpv4Addresses);
};

// rk_app/src/healthd/host/host_wifi_status_provider.cpp
#include "host/host_wifi_status_provider.h"

#include <QNetworkInterface>
#include <QProcess>

namespace {
QString firstUsableIpv4(const QList<QHostAddress> &addresses) {
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isNull()) {
            return address.toString();
        }
    }
    return QStringLiteral("--");
}
}

HostWifiStatusProvider::HostWifiStatusProvider(QObject *parent)
    : QObject(parent) {}

NmcliHostWifiStatusProvider::NmcliHostWifiStatusProvider(QObject *parent)
    : HostWifiStatusProvider(parent) {}

HostWifiStatus NmcliHostWifiStatusProvider::fallbackStatus(
    const QString &fallbackInterfaceName, const QList<QHostAddress> &fallbackIpv4Addresses) {
    HostWifiStatus status;
    status.present = !fallbackInterfaceName.isEmpty();
    status.connected = false;
    status.interfaceName = status.present ? fallbackInterfaceName : QStringLiteral("--");
    status.ssid = QStringLiteral("--");
    status.ipv4 = firstUsableIpv4(fallbackIpv4Addresses);
    return status;
}
```

- [ ] **Step 4: Finish the real probe logic and register targets**

```cmake
// rk_app/src/healthd/host/host_wifi_status_provider.cpp
HostWifiStatus NmcliHostWifiStatusProvider::statusFromProbeOutputs(
    const QByteArray &nmcliDeviceOutput, const QString &fallbackInterfaceName,
    const QList<QHostAddress> &fallbackIpv4Addresses) {
    const QList<QByteArray> rows = nmcliDeviceOutput.split('\n');
    for (const QByteArray &row : rows) {
        const QList<QByteArray> fields = row.trimmed().split(':');
        if (fields.size() < 4 || fields.at(1) != "wifi") {
            continue;
        }

        HostWifiStatus status;
        status.present = true;
        status.connected = fields.at(0) == "yes";
        status.interfaceName = QString::fromUtf8(fields.at(2));
        status.ssid = status.connected && !fields.at(3).isEmpty()
            ? QString::fromUtf8(fields.at(3))
            : QStringLiteral("--");
        status.ipv4 = status.connected
            ? firstUsableIpv4(fallbackIpv4Addresses)
            : QStringLiteral("--");
        return status;
    }

    return fallbackStatus(fallbackInterfaceName, fallbackIpv4Addresses);
}

HostWifiStatus NmcliHostWifiStatusProvider::currentStatus() const {
    QProcess nmcli;
    nmcli.start(QStringLiteral("nmcli"),
        {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("ACTIVE,TYPE,DEVICE,SSID"),
            QStringLiteral("device")});
    nmcli.waitForFinished(1500);

    QString fallbackInterfaceName;
    QList<QHostAddress> fallbackIpv4Addresses;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!iface.name().contains(QStringLiteral("wl"))) {
            continue;
        }
        fallbackInterfaceName = iface.name();
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            fallbackIpv4Addresses.push_back(entry.ip());
        }
        break;
    }

    return statusFromProbeOutputs(
        nmcli.readAllStandardOutput(), fallbackInterfaceName, fallbackIpv4Addresses);
}

# rk_app/src/healthd/CMakeLists.txt
add_executable(healthd
    main.cpp
    app/daemon_app.cpp
    app/daemon_app.h
    alerts/alert_engine.cpp
    alerts/alert_engine.h
    device/auth_manager.cpp
    device/auth_manager.h
    device/device_manager.cpp
    device/device_manager.h
    host/host_wifi_status.h
    host/host_wifi_status_provider.cpp
    host/host_wifi_status_provider.h
    ipc_server/ui_gateway.cpp
    ipc_server/ui_gateway.h
    network/tcp_acceptor.cpp
    network/tcp_acceptor.h
    telemetry/aggregation_service.cpp
    telemetry/aggregation_service.h
    telemetry/telemetry_service.cpp
    telemetry/telemetry_service.h
)

# rk_app/src/tests/CMakeLists.txt
add_executable(host_wifi_status_provider_test
    host_tests/host_wifi_status_provider_test.cpp
    ../healthd/host/host_wifi_status_provider.cpp
)
target_include_directories(host_wifi_status_provider_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../healthd
)
target_link_libraries(host_wifi_status_provider_test PRIVATE
    ${RK_QT_CORE_TARGET}
    ${RK_QT_NETWORK_TARGET}
    ${RK_QT_TEST_TARGET}
)
add_test(NAME host_wifi_status_provider_test COMMAND host_wifi_status_provider_test)
```

- [ ] **Step 5: Run the focused provider test and make sure it passes**

Run: `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON && ctest --test-dir out/build-rk_app-host -R host_wifi_status_provider_test --output-on-failure`

Expected: `100% tests passed` and `host_wifi_status_provider_test` covers connected, malformed-output fallback, disconnected NIC, and no-NIC behavior.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/healthd/CMakeLists.txt rk_app/src/healthd/host rk_app/src/tests/CMakeLists.txt rk_app/src/tests/host_tests/host_wifi_status_provider_test.cpp
git commit -m "feat: add host wifi status provider"
```

### Task 2: Inject host Wi-Fi state into `healthd` dashboard snapshots

**Files:**
- Modify: `rk_app/src/healthd/app/daemon_app.h`
- Modify: `rk_app/src/healthd/app/daemon_app.cpp`
- Modify: `rk_app/src/healthd/ipc_server/ui_gateway.h`
- Modify: `rk_app/src/healthd/ipc_server/ui_gateway.cpp`
- Modify: `rk_app/src/tests/ipc_tests/ui_gateway_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Extend the gateway test to fail first**

```cpp
void UiGatewayTest::requestDashboardSnapshotIncludesHostWifi() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("RK_HEALTH_STATION_SOCKET_NAME",
        tempDir.filePath(QStringLiteral("rk_health_station.sock")).toUtf8());

    DeviceManager deviceManager;
    class StubHostWifiStatusProvider final : public HostWifiStatusProvider {
    public:
        using HostWifiStatusProvider::HostWifiStatusProvider;
        HostWifiStatus value;

        HostWifiStatus currentStatus() const override { return value; }
    };

    StubHostWifiStatusProvider provider;
    provider.value.present = true;
    provider.value.connected = false;
    provider.value.interfaceName = QStringLiteral("wlan0");
    provider.value.ssid = QStringLiteral("--");
    provider.value.ipv4 = QStringLiteral("--");

    UiGateway gateway(&deviceManager, nullptr, &provider);
    QVERIFY(gateway.start());

    QLocalSocket socket;
    socket.connectToServer(qEnvironmentVariable("RK_HEALTH_STATION_SOCKET_NAME"));
    QVERIFY(socket.waitForConnected(3000));

    IpcMessage req {1, QStringLiteral("request"), QStringLiteral("get_dashboard_snapshot"),
        QStringLiteral("req-dashboard-1"), true, {}};
    socket.write(IpcCodec::encode(req));
    QVERIFY(socket.waitForBytesWritten(3000));
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0, 3000);

    IpcMessage response;
    QVERIFY(IpcCodec::decode(socket.readAll(), &response));
    QVERIFY(response.payload.contains(QStringLiteral("host_wifi")));
}
```

- [ ] **Step 2: Run the gateway test and verify it fails**

Run: `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON && ctest --test-dir out/build-rk_app-host -R ui_gateway_test --output-on-failure`

Expected: build fails because `UiGateway` does not yet accept a provider and dashboard payload has no `host_wifi`.

- [ ] **Step 3: Inject the provider through `DaemonApp` and `UiGateway`**

```cpp
// rk_app/src/healthd/app/daemon_app.h
#include "host/host_wifi_status_provider.h"

class DaemonApp : public QObject {
    Q_OBJECT
private:
    TcpAcceptor acceptor_;
    Database database_;
    AuthManager authManager_;
    DeviceManager deviceManager_;
    NmcliHostWifiStatusProvider hostWifiStatusProvider_;
    UiGateway uiGateway_;
    TelemetryService telemetryService_;
    bool started_ = false;
    QString databasePath_;
    QString markerPath_;
    bool markerEnabledByEnv_ = false;
};

// rk_app/src/healthd/app/daemon_app.cpp
DaemonApp::DaemonApp(QObject *parent)
    : QObject(parent)
    , deviceManager_(&database_)
    , hostWifiStatusProvider_(this)
    , uiGateway_(&deviceManager_, &database_, &hostWifiStatusProvider_, this)
    , telemetryService_(&deviceManager_, &database_) {
    connect(&acceptor_, &TcpAcceptor::envelopeReceived,
        this, &DaemonApp::onEnvelopeReceived);
}

// rk_app/src/healthd/ipc_server/ui_gateway.h
class HostWifiStatusProvider;

class UiGateway : public QObject {
    Q_OBJECT
public:
    explicit UiGateway(DeviceManager *deviceManager, Database *database = nullptr,
        HostWifiStatusProvider *hostWifiStatusProvider = nullptr, QObject *parent = nullptr);
private:
    static QJsonObject hostWifiToJson(const HostWifiStatus &status);
    HostWifiStatusProvider *hostWifiStatusProvider_ = nullptr;
};
```

- [ ] **Step 4: Append `host_wifi` to dashboard responses**

```cpp
// rk_app/src/healthd/ipc_server/ui_gateway.cpp
#include "host/host_wifi_status_provider.h"

UiGateway::UiGateway(DeviceManager *deviceManager, Database *database,
    HostWifiStatusProvider *hostWifiStatusProvider, QObject *parent)
    : QObject(parent)
    , deviceManager_(deviceManager)
    , database_(database)
    , server_(new QLocalServer(this))
    , hostWifiStatusProvider_(hostWifiStatusProvider) {
    connect(server_, &QLocalServer::newConnection, this, &UiGateway::onNewConnection);
}

QJsonObject UiGateway::hostWifiToJson(const HostWifiStatus &status) {
    QJsonObject object;
    object.insert(QStringLiteral("present"), status.present);
    object.insert(QStringLiteral("connected"), status.connected);
    object.insert(QStringLiteral("interface_name"), status.interfaceName);
    object.insert(QStringLiteral("ssid"), status.ssid);
    object.insert(QStringLiteral("ipv4"), status.ipv4);
    return object;
}

IpcMessage UiGateway::buildDashboardResponse(const QString &reqId) const {
    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = QStringLiteral("get_dashboard_snapshot");
    response.reqId = reqId;
    response.ok = true;

    QJsonArray devices;
    QString currentDeviceId;
    if (deviceManager_) {
        const QList<DeviceManager::DeviceRecord> records = deviceManager_->allDevices();
        for (const DeviceManager::DeviceRecord &record : records) {
            appendDeviceJson(&devices, record.info.deviceId, record.info.deviceName,
                deviceLifecycleStateToString(record.info.status), record.runtime.online,
                record.runtime.lastSeenAt, record.runtime.remoteIp);
            if (currentDeviceId.isEmpty() && record.info.status == DeviceLifecycleState::Active) {
                currentDeviceId = record.info.deviceId;
            }
        }
    }
    const HostWifiStatus hostWifi = hostWifiStatusProvider_
        ? hostWifiStatusProvider_->currentStatus()
        : HostWifiStatus {};
    response.payload.insert(QStringLiteral("device_count"), devices.size());
    response.payload.insert(QStringLiteral("current_device_id"), currentDeviceId);
    response.payload.insert(QStringLiteral("devices"), devices);
    response.payload.insert(QStringLiteral("host_wifi"), hostWifiToJson(hostWifi));
    return response;
}
```

- [ ] **Step 5: Run the gateway test suite and make sure it passes**

Run: `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON && ctest --test-dir out/build-rk_app-host -R ui_gateway_test --output-on-failure`

Expected: `ui_gateway_test` passes and dashboard snapshot payload now includes `host_wifi` without breaking existing device-list or history cases.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/healthd/app/daemon_app.h rk_app/src/healthd/app/daemon_app.cpp rk_app/src/healthd/ipc_server/ui_gateway.h rk_app/src/healthd/ipc_server/ui_gateway.cpp rk_app/src/tests/ipc_tests/ui_gateway_test.cpp rk_app/src/tests/CMakeLists.txt
git commit -m "feat: expose host wifi state in dashboard snapshot"
```

### Task 3: Render the Wi-Fi card on `DashboardPage`

**Files:**
- Modify: `rk_app/src/health_ui/pages/dashboard_page.h`
- Modify: `rk_app/src/health_ui/pages/dashboard_page.cpp`
- Create: `rk_app/src/tests/ui_tests/dashboard_page_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing dashboard page tests**

```cpp
#include "pages/dashboard_page.h"

#include <QtTest/QTest>

class DashboardPageTest : public QObject {
    Q_OBJECT

private slots:
    void rendersConnectedWifiState();
    void rendersDisconnectedWifiState();
    void rendersNoWifiNicState();
};

void DashboardPageTest::rendersConnectedWifiState() {
    DashboardPage page;
    QJsonObject snapshot {
        {QStringLiteral("host_wifi"), QJsonObject {
             {QStringLiteral("present"), true},
             {QStringLiteral("connected"), true},
             {QStringLiteral("interface_name"), QStringLiteral("wlan0")},
             {QStringLiteral("ssid"), QStringLiteral("Office_AP")},
             {QStringLiteral("ipv4"), QStringLiteral("192.168.137.23")},
         }},
    };

    page.setSnapshot(snapshot);

    QCOMPARE(page.wifiStatusText(), QStringLiteral("已连接"));
    QCOMPARE(page.wifiSsidText(), QStringLiteral("Office_AP"));
    QCOMPARE(page.wifiInterfaceText(), QStringLiteral("wlan0"));
    QCOMPARE(page.wifiIpv4Text(), QStringLiteral("192.168.137.23"));
}

void DashboardPageTest::rendersDisconnectedWifiState() {
    DashboardPage page;
    QJsonObject snapshot {
        {QStringLiteral("host_wifi"), QJsonObject {
             {QStringLiteral("present"), true},
             {QStringLiteral("connected"), false},
             {QStringLiteral("interface_name"), QStringLiteral("wlan0")},
             {QStringLiteral("ssid"), QStringLiteral("--")},
             {QStringLiteral("ipv4"), QStringLiteral("--")},
         }},
    };

    page.setSnapshot(snapshot);

    QCOMPARE(page.wifiStatusText(), QStringLiteral("未连接"));
    QCOMPARE(page.wifiInterfaceText(), QStringLiteral("wlan0"));
    QCOMPARE(page.wifiIpv4Text(), QStringLiteral("--"));
}

void DashboardPageTest::rendersNoWifiNicState() {
    DashboardPage page;
    QJsonObject snapshot {
        {QStringLiteral("host_wifi"), QJsonObject {
             {QStringLiteral("present"), false},
             {QStringLiteral("connected"), false},
             {QStringLiteral("interface_name"), QStringLiteral("--")},
             {QStringLiteral("ssid"), QStringLiteral("--")},
             {QStringLiteral("ipv4"), QStringLiteral("--")},
         }},
    };

    page.setSnapshot(snapshot);

    QCOMPARE(page.wifiStatusText(), QStringLiteral("无 Wi-Fi 网卡"));
    QCOMPARE(page.wifiSsidText(), QStringLiteral("--"));
    QCOMPARE(page.wifiIpv4Text(), QStringLiteral("--"));
}
```

- [ ] **Step 2: Run the dashboard page test and verify it fails**

Run: `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON && ctest --test-dir out/build-rk_app-host -R dashboard_page_test --output-on-failure`

Expected: build fails because `DashboardPage` does not yet expose Wi-Fi labels or the new test target.

- [ ] **Step 3: Add the Wi-Fi card and test accessors**

```cpp
// rk_app/src/health_ui/pages/dashboard_page.h
class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(QWidget *parent = nullptr);
    void setSnapshot(const QJsonObject &snapshot);
    QString wifiStatusText() const;
    QString wifiSsidText() const;
    QString wifiInterfaceText() const;
    QString wifiIpv4Text() const;
private:
    void setField(QLabel *label, const QString &name, const QString &value);
    QLabel *deviceNameLabel_ = nullptr;
    QLabel *onlineLabel_ = nullptr;
    QLabel *heartRateLabel_ = nullptr;
    QLabel *spo2Label_ = nullptr;
    QLabel *batteryLabel_ = nullptr;
    QLabel *updatedAtLabel_ = nullptr;
    QLabel *wifiStatusLabel_ = nullptr;
    QLabel *wifiSsidLabel_ = nullptr;
    QLabel *wifiInterfaceLabel_ = nullptr;
    QLabel *wifiIpv4Label_ = nullptr;
};

// rk_app/src/health_ui/pages/dashboard_page.cpp
DashboardPage::DashboardPage(QWidget *parent)
    : QWidget(parent)
    , deviceNameLabel_(new QLabel(QStringLiteral("--"), this))
    , onlineLabel_(new QLabel(QStringLiteral("--"), this))
    , heartRateLabel_(new QLabel(QStringLiteral("--"), this))
    , spo2Label_(new QLabel(QStringLiteral("--"), this))
    , batteryLabel_(new QLabel(QStringLiteral("--"), this))
    , updatedAtLabel_(new QLabel(QStringLiteral("--"), this))
    , wifiStatusLabel_(new QLabel(QStringLiteral("--"), this))
    , wifiSsidLabel_(new QLabel(QStringLiteral("--"), this))
    , wifiInterfaceLabel_(new QLabel(QStringLiteral("--"), this))
    , wifiIpv4Label_(new QLabel(QStringLiteral("--"), this)) {
    auto *layout = new QFormLayout(this);
    layout->addRow(QStringLiteral("device_name"), deviceNameLabel_);
    layout->addRow(QStringLiteral("online"), onlineLabel_);
    layout->addRow(QStringLiteral("heart_rate"), heartRateLabel_);
    layout->addRow(QStringLiteral("spo2"), spo2Label_);
    layout->addRow(QStringLiteral("battery"), batteryLabel_);
    layout->addRow(QStringLiteral("updated_at"), updatedAtLabel_);
    layout->addRow(QStringLiteral("status"), wifiStatusLabel_);
    layout->addRow(QStringLiteral("ssid"), wifiSsidLabel_);
    layout->addRow(QStringLiteral("interface"), wifiInterfaceLabel_);
    layout->addRow(QStringLiteral("ipv4 (for ESP)"), wifiIpv4Label_);
}
```

- [ ] **Step 4: Map `host_wifi` JSON into display strings and register the test**

```cpp
void DashboardPage::setSnapshot(const QJsonObject &snapshot) {
    QJsonObject currentDevice = snapshot.value(QStringLiteral("current_device")).toObject();
    if (currentDevice.isEmpty()) {
        const QJsonArray devices = snapshot.value(QStringLiteral("devices")).toArray();
        if (!devices.isEmpty()) {
            currentDevice = devices.first().toObject();
        }
    }

    setField(deviceNameLabel_, QStringLiteral("device_name"),
        currentDevice.value(QStringLiteral("device_name")).toString(QStringLiteral("--")));
    setField(onlineLabel_, QStringLiteral("online"),
        currentDevice.contains(QStringLiteral("online"))
            ? (currentDevice.value(QStringLiteral("online")).toBool() ? QStringLiteral("true")
                                                                      : QStringLiteral("false"))
            : QStringLiteral("--"));
    const QJsonObject hostWifi = snapshot.value(QStringLiteral("host_wifi")).toObject();
    const bool present = hostWifi.value(QStringLiteral("present")).toBool(false);
    const bool connected = hostWifi.value(QStringLiteral("connected")).toBool(false);

    const QString wifiStatus = !present ? QStringLiteral("无 Wi-Fi 网卡")
        : (connected ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    setField(wifiStatusLabel_, QStringLiteral("status"), wifiStatus);
    setField(wifiSsidLabel_, QStringLiteral("ssid"),
        hostWifi.value(QStringLiteral("ssid")).toString(QStringLiteral("--")));
    setField(wifiInterfaceLabel_, QStringLiteral("interface"),
        hostWifi.value(QStringLiteral("interface_name")).toString(QStringLiteral("--")));
    setField(wifiIpv4Label_, QStringLiteral("ipv4"),
        hostWifi.value(QStringLiteral("ipv4")).toString(QStringLiteral("--")));
}

QString DashboardPage::wifiStatusText() const { return wifiStatusLabel_->text(); }
QString DashboardPage::wifiSsidText() const { return wifiSsidLabel_->text(); }
QString DashboardPage::wifiInterfaceText() const { return wifiInterfaceLabel_->text(); }
QString DashboardPage::wifiIpv4Text() const { return wifiIpv4Label_->text(); }

// rk_app/src/tests/CMakeLists.txt
add_executable(dashboard_page_test
    ui_tests/dashboard_page_test.cpp
    ../health_ui/pages/dashboard_page.cpp
)
target_include_directories(dashboard_page_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_ui
)
target_link_libraries(dashboard_page_test PRIVATE
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)
add_test(NAME dashboard_page_test COMMAND dashboard_page_test)
```

- [ ] **Step 5: Run the UI rendering test and make sure it passes**

Run: `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON && ctest --test-dir out/build-rk_app-host -R dashboard_page_test --output-on-failure`

Expected: `dashboard_page_test` passes for connected and disconnected states, and the page stays stable when `host_wifi` is missing.

- [ ] **Step 6: Commit**

```bash
git add rk_app/src/health_ui/pages/dashboard_page.h rk_app/src/health_ui/pages/dashboard_page.cpp rk_app/src/tests/ui_tests/dashboard_page_test.cpp rk_app/src/tests/CMakeLists.txt
git commit -m "feat: show host wifi status on dashboard"
```

### Task 4: Refresh the dashboard snapshot every 3 seconds and verify end-to-end behavior

**Files:**
- Modify: `rk_app/src/health_ui/app/ui_app.h`
- Modify: `rk_app/src/health_ui/app/ui_app.cpp`
- Create: `rk_app/src/tests/ui_tests/ui_app_dashboard_refresh_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing refresh test**

```cpp
#include "app/ui_app.h"
#include "protocol/ipc_message.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QtTest/QTest>

class UiAppDashboardRefreshTest : public QObject {
    Q_OBJECT

private slots:
    void requestsDashboardSnapshotAgainAfterTimerTick();
};

void UiAppDashboardRefreshTest::requestsDashboardSnapshotAgainAfterTimerTick() {
    QLocalServer server;
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString socketPath = tempDir.filePath(QStringLiteral("rk_health_station.sock"));
    qputenv("RK_HEALTH_STATION_SOCKET_NAME", socketPath.toUtf8());
    QVERIFY(server.listen(socketPath));

    UiApp app;
    QVERIFY(app.start());

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);

    int dashboardRequests = 0;
    QObject::connect(socket, &QLocalSocket::readyRead, this, [&dashboardRequests, socket]() {
        const QList<QByteArray> frames = socket->readAll().split('\n');
        for (const QByteArray &frame : frames) {
            IpcMessage message;
            if (frame.trimmed().isEmpty() || !ipcMessageFromJson(QJsonDocument::fromJson(frame).object(), &message)) {
                continue;
            }
            if (message.action == QStringLiteral("get_dashboard_snapshot")) {
                ++dashboardRequests;
            }
        }
    });

    QTRY_VERIFY_WITH_TIMEOUT(dashboardRequests >= 2, 4500);
}
```

- [ ] **Step 2: Run the refresh test and verify it fails**

Run: `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON && ctest --test-dir out/build-rk_app-host -R ui_app_dashboard_refresh_test --output-on-failure`

Expected: build fails because there is no refresh timer and the new UI test target is not registered.

- [ ] **Step 3: Add a dashboard refresh timer to `UiApp`**

```cpp
// rk_app/src/health_ui/app/ui_app.h
class QTimer;

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

// rk_app/src/health_ui/app/ui_app.cpp
#include <QTimer>

namespace {
constexpr int kDashboardRefreshMs = 3000;
}

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
    , videoMonitorPage_(new VideoMonitorPage(
          videoClient_,
          fallClient_,
          stack_,
          [this]() {
              return QFileDialog::getOpenFileName(
                  window_,
                  QStringLiteral("Select Test Video"),
                  QString(),
                  QStringLiteral("Video Files (*.mp4 *.MP4);;All Files (*)"));
          }))
    , dashboardRefreshTimer_(new QTimer(this)) {
    dashboardRefreshTimer_->setInterval(kDashboardRefreshMs);
    connect(dashboardRefreshTimer_, &QTimer::timeout, this, [this]() {
        if (client_->isConnected()) {
            client_->requestDashboardSnapshot();
        }
    });
}

bool UiApp::start() {
    window_->show();
    const bool connected = client_->connectToBackend();
    if (connected) {
        client_->requestDeviceList();
        client_->requestDashboardSnapshot();
        client_->requestPendingDevices();
        client_->requestAlertsSnapshot();
        dashboardRefreshTimer_->start();
    }
    return connected;
}
```

- [ ] **Step 4: Register the refresh test and strengthen the smoke test**

```cmake
# rk_app/src/tests/CMakeLists.txt
add_executable(ui_app_dashboard_refresh_test
    ui_tests/ui_app_dashboard_refresh_test.cpp
    ../health_ui/app/ui_app.cpp
    ../health_ui/ipc_client/fall_ipc_client.cpp
    ../health_ui/ipc_client/ui_ipc_client.cpp
    ../health_ui/ipc_client/video_ipc_client.cpp
    ../health_ui/pages/alerts_page.cpp
    ../health_ui/pages/dashboard_page.cpp
    ../health_ui/pages/device_approval_page.cpp
    ../health_ui/pages/device_detail_page.cpp
    ../health_ui/pages/device_list_page.cpp
    ../health_ui/pages/history_page.cpp
    ../health_ui/pages/settings_page.cpp
    ../health_ui/pages/video_monitor_page.cpp
    ../health_ui/widgets/video_preview_consumer.cpp
    ../health_ui/widgets/video_preview_widget.cpp
)
target_include_directories(ui_app_dashboard_refresh_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../health_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)
target_link_libraries(ui_app_dashboard_refresh_test PRIVATE
    rk_shared
    ${RK_QT_CORE_TARGET}
    ${RK_QT_NETWORK_TARGET}
    ${RK_QT_TEST_TARGET}
    ${RK_QT_WIDGETS_TARGET}
)
if(RK_HAS_QT_MULTIMEDIA)
    target_link_libraries(ui_app_dashboard_refresh_test PRIVATE
        ${RK_QT_MULTIMEDIA_TARGET}
        ${RK_QT_MULTIMEDIA_WIDGETS_TARGET}
    )
endif()
target_compile_definitions(ui_app_dashboard_refresh_test PRIVATE RK_HAS_QT_MULTIMEDIA=${RK_HAS_QT_MULTIMEDIA})
add_test(NAME ui_app_dashboard_refresh_test COMMAND ui_app_dashboard_refresh_test)
```

- [ ] **Step 5: Run the focused UI tests and then the full validation set**

Run: `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON && ctest --test-dir out/build-rk_app-host -R "host_wifi_status_provider_test|ui_gateway_test|dashboard_page_test|health_ui_smoke_test|ui_app_dashboard_refresh_test" --output-on-failure`

Expected: all five tests pass, proving provider parsing, dashboard IPC, widget rendering, backend connection, and 3-second refresh behavior.

- [ ] **Step 6: Perform the final manual verification on the RK3588 board**

Run:

```bash
bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON
ctest --test-dir out/build-rk_app-host -R "host_wifi_status_provider_test|ui_gateway_test|dashboard_page_test|health_ui_smoke_test|ui_app_dashboard_refresh_test" --output-on-failure
```

Expected manual board checks:

- disconnected Wi-Fi shows `未连接`, `--`, real interface name, and `--`
- connected Wi-Fi shows `已连接`, real SSID, real interface, and the current Wi-Fi IPv4
- disconnecting Wi-Fi updates the card within roughly 3 seconds
- no Wi-Fi NIC shows `无 Wi-Fi 网卡`
- the displayed `ipv4 (for ESP)` is the address entered into ESP provisioning

- [ ] **Step 7: Commit**

```bash
git add rk_app/src/health_ui/app/ui_app.h rk_app/src/health_ui/app/ui_app.cpp rk_app/src/tests/ui_tests/ui_app_dashboard_refresh_test.cpp rk_app/src/tests/CMakeLists.txt
git commit -m "feat: refresh dashboard wifi status automatically"
```

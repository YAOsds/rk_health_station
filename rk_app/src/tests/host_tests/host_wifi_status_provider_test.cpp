#include "host/host_wifi_status_provider.h"

#include <QHostAddress>
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
        = NmcliHostWifiStatusProvider::statusFromProbeOutputs(
            deviceOutput, QStringLiteral("wlan0"), addrs);

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

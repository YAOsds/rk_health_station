#include "pages/dashboard_page.h"

#include <QJsonObject>
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
    QCOMPARE(page.wifiSsidText(), QStringLiteral("--"));
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
    QCOMPARE(page.wifiInterfaceText(), QStringLiteral("--"));
    QCOMPARE(page.wifiIpv4Text(), QStringLiteral("--"));
}

QTEST_MAIN(DashboardPageTest)

#include "dashboard_page_test.moc"

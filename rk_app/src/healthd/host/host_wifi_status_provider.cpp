#include "host/host_wifi_status_provider.h"

#include <QAbstractSocket>
#include <QNetworkAddressEntry>
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
    status.ipv4 = status.present ? firstUsableIpv4(fallbackIpv4Addresses) : QStringLiteral("--");
    return status;
}

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
        status.ipv4 = status.connected ? firstUsableIpv4(fallbackIpv4Addresses)
                                       : QStringLiteral("--");
        return status;
    }

    return fallbackStatus(fallbackInterfaceName, fallbackIpv4Addresses);
}

HostWifiStatus NmcliHostWifiStatusProvider::currentStatus() const {
    QProcess nmcli;
    nmcli.start(QStringLiteral("nmcli"),
        {QStringLiteral("-t"), QStringLiteral("-f"),
            QStringLiteral("ACTIVE,TYPE,DEVICE,SSID"), QStringLiteral("device")});
    nmcli.waitForFinished(1500);

    QString fallbackInterfaceName;
    QList<QHostAddress> fallbackIpv4Addresses;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!iface.name().contains(QStringLiteral("wl"))) {
            continue;
        }
        fallbackInterfaceName = iface.name();
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            fallbackIpv4Addresses.push_back(entry.ip());
        }
        break;
    }

    return statusFromProbeOutputs(
        nmcli.readAllStandardOutput(), fallbackInterfaceName, fallbackIpv4Addresses);
}

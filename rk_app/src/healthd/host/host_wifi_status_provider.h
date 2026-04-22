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

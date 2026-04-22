#pragma once

#include <QString>

struct HostWifiStatus {
    bool present = false;
    bool connected = false;
    QString interfaceName = QStringLiteral("--");
    QString ssid = QStringLiteral("--");
    QString ipv4 = QStringLiteral("--");
};

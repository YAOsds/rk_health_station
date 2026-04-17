#pragma once

#include <QString>
#include <QtGlobal>

enum class DeviceLifecycleState {
    Pending,
    Active,
    Disabled,
    Revoked,
    Offline,
};

inline QString deviceLifecycleStateToString(DeviceLifecycleState state) {
    switch (state) {
    case DeviceLifecycleState::Pending:
        return QStringLiteral("pending");
    case DeviceLifecycleState::Active:
        return QStringLiteral("active");
    case DeviceLifecycleState::Disabled:
        return QStringLiteral("disabled");
    case DeviceLifecycleState::Revoked:
        return QStringLiteral("revoked");
    case DeviceLifecycleState::Offline:
        return QStringLiteral("offline");
    }
    return QStringLiteral("pending");
}

struct DeviceInfo {
    QString deviceId;
    QString deviceName;
    QString deviceSecret;
    DeviceLifecycleState status = DeviceLifecycleState::Pending;
    QString bindMode;
    QString model;
    QString firmwareVersion;
};

struct DeviceStatus {
    QString deviceId;
    DeviceLifecycleState status = DeviceLifecycleState::Offline;
    bool online = false;
    qint64 lastSeenAt = 0;
    QString remoteIp;
};

#include "app/daemon_app.h"

#include "network/device_session.h"
#include "runtime_config/app_runtime_config_loader.h"

#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>

namespace {
constexpr quint16 kHealthdPort = 19001;
const char kDefaultDatabasePath[] = "/tmp/healthd-task4.sqlite";
const char kDefaultMarkerPath[] = "/tmp/healthd-task3-marker.jsonl";
const char kMarkerEnablePath[] = "/tmp/healthd-task3-marker.enable";

QString helloDecisionToString(AuthManager::HelloDecision decision) {
    switch (decision) {
    case AuthManager::HelloDecision::SendChallenge:
        return QStringLiteral("send_challenge");
    case AuthManager::HelloDecision::RegistrationRequired:
        return QStringLiteral("registration_required");
    case AuthManager::HelloDecision::Rejected:
        return QStringLiteral("rejected");
    }
    return QStringLiteral("unknown");
}
}

DaemonApp::DaemonApp(const AppRuntimeConfig &config, QObject *parent)
    : QObject(parent)
    , config_(config)
    , deviceManager_(&database_)
    , hostWifiStatusProvider_(this)
    , uiGateway_(config.ipc.healthSocketPath, &deviceManager_, &database_, &hostWifiStatusProvider_, this)
    , telemetryService_(&deviceManager_, &database_)
    , databasePath_(config.paths.databasePath)
    , markerPath_(config.debug.healthdEventMarkerPath) {
    connect(&acceptor_, &TcpAcceptor::envelopeReceived,
        this, &DaemonApp::onEnvelopeReceived);

    if (databasePath_.isEmpty()) {
        databasePath_ = QString::fromUtf8(kDefaultDatabasePath);
    }

    markerEnabledByEnv_ = !markerPath_.isEmpty();
    if (!markerEnabledByEnv_) {
        markerPath_ = QString::fromUtf8(kDefaultMarkerPath);
    }
    if (markerEnabledByEnv_) {
        qInfo() << "healthd telemetry marker path enabled:" << markerPath_;
    }

    qInfo() << "healthd lifecycle: daemon constructed"
            << "db_path=" << databasePath_
            << "socket_name=" << config_.ipc.healthSocketPath
            << "tcp_port=" << kHealthdPort
            << "marker_path=" << markerPath_
            << "marker_enabled=" << isMarkerWritingEnabled();
}

DaemonApp::DaemonApp(QObject *parent)
    : DaemonApp(loadAppRuntimeConfig(QString()).config, parent) {
}

bool DaemonApp::start() {
    if (started_) {
        qInfo() << "healthd lifecycle: start requested while already started";
        return true;
    }

    qInfo() << "healthd lifecycle: bootstrap begin";
    QString error;
    if (!database_.open(databasePath_, &error)) {
        qCritical() << "healthd failed to open database" << databasePath_ << error;
        return false;
    }
    if (!database_.initializeSchema(&error)) {
        qCritical() << "healthd failed to initialize database schema" << error;
        return false;
    }

    const QStringList persistedDeviceIds = database_.listDeviceIds(&error);
    if (!error.isEmpty()) {
        qCritical() << "healthd failed to enumerate persisted devices" << error;
        return false;
    }
    for (const QString &deviceId : persistedDeviceIds) {
        deviceManager_.reloadFromDatabase(deviceId);
    }

    qInfo() << "healthd database ready at" << databasePath_;
    qInfo() << "healthd lifecycle: persisted device state loaded"
            << "device_count=" << persistedDeviceIds.size();

    if (!uiGateway_.start()) {
        qCritical() << "healthd failed to start local ui gateway";
        return false;
    }
    qInfo() << "healthd lifecycle: local ui gateway ready"
            << "socket_name=" << config_.ipc.healthSocketPath;

    started_ = acceptor_.start(kHealthdPort);
    if (started_) {
        qInfo() << "healthd listening on TCP port" << kHealthdPort;
        qInfo() << "healthd lifecycle: bootstrap complete";
    } else {
        qCritical() << "healthd failed to listen on TCP port" << kHealthdPort;
    }
    return started_;
}

void DaemonApp::onEnvelopeReceived(
    DeviceSession *session, const DeviceEnvelope &envelope, const QString &remoteIp) {
    if (!session) {
        qWarning() << "healthd dropped frame without session" << envelope.type;
        return;
    }

    if (envelope.type == QStringLiteral("auth_hello")) {
        qInfo() << "healthd lifecycle: received auth_hello"
                << "device_id=" << envelope.deviceId
                << "remote_ip=" << remoteIp;
        handleAuthHello(session, envelope, remoteIp);
        return;
    }
    if (envelope.type == QStringLiteral("auth_proof")) {
        qInfo() << "healthd lifecycle: received auth_proof"
                << "device_id=" << envelope.deviceId
                << "remote_ip=" << remoteIp;
        handleAuthProof(session, envelope);
        return;
    }

    if (envelope.type != QStringLiteral("telemetry_batch")) {
        qInfo() << "healthd received unsupported frame type" << envelope.type;
        return;
    }

    if (session->authState() != DeviceSession::SessionAuthState::Active
        || session->authenticatedDeviceId() != envelope.deviceId) {
        qWarning() << "healthd dropped unauthenticated telemetry for device" << envelope.deviceId;
        return;
    }

    const QByteArray payloadJson = QJsonDocument(envelope.payload).toJson(QJsonDocument::Compact);
    qInfo().noquote()
        << QStringLiteral("telemetry ver=%1 type=%2 seq=%3 ts=%4 device_id=%5 payload=%6")
               .arg(envelope.ver)
               .arg(envelope.type)
               .arg(envelope.seq)
               .arg(envelope.ts)
               .arg(envelope.deviceId)
               .arg(QString::fromUtf8(payloadJson));

    if (!telemetryService_.handleTelemetry(envelope, remoteIp)) {
        qWarning() << "healthd failed to process telemetry for device" << envelope.deviceId;
    }

    writeTelemetryMarker(envelope);
}

bool DaemonApp::handleAuthHello(
    DeviceSession *session, const DeviceEnvelope &envelope, const QString &remoteIp) {
    QString error;
    const AuthManager::HelloResult result
        = authManager_.handleAuthHello(envelope, remoteIp, &database_, &error);
    if (!error.isEmpty()) {
        qWarning() << "healthd auth hello failed for device" << envelope.deviceId << error;
    }

    qInfo() << "healthd lifecycle: auth_hello decision"
            << "device_id=" << envelope.deviceId
            << "remote_ip=" << remoteIp
            << "decision=" << helloDecisionToString(result.decision);

    session->setAuthenticatedDeviceId(envelope.deviceId);
    switch (result.decision) {
    case AuthManager::HelloDecision::SendChallenge:
        session->setServerNonce(result.serverNonce);
        session->setAuthState(DeviceSession::SessionAuthState::ChallengeSent);
        return sendAuthChallenge(session, envelope, result.serverNonce);
    case AuthManager::HelloDecision::RegistrationRequired:
        session->setServerNonce(QString());
        session->setAuthState(DeviceSession::SessionAuthState::PendingApproval);
        return sendAuthResult(session, envelope, QStringLiteral("registration_required"));
    case AuthManager::HelloDecision::Rejected:
        session->setServerNonce(QString());
        session->setAuthState(DeviceSession::SessionAuthState::Rejected);
        return sendAuthResult(session, envelope, QStringLiteral("rejected"));
    }

    return false;
}

bool DaemonApp::handleAuthProof(DeviceSession *session, const DeviceEnvelope &envelope) {
    if (!session || session->authState() != DeviceSession::SessionAuthState::ChallengeSent
        || session->authenticatedDeviceId() != envelope.deviceId || session->serverNonce().isEmpty()) {
        qWarning() << "healthd dropped auth_proof without pending challenge for" << envelope.deviceId;
        return sendAuthResult(session, envelope, QStringLiteral("rejected"));
    }

    Database::StoredDevice stored;
    QString error;
    if (!database_.fetchDevice(envelope.deviceId, &stored, &error)) {
        qWarning() << "healthd dropped auth_proof for unknown device" << envelope.deviceId << error;
        session->setAuthState(DeviceSession::SessionAuthState::Rejected);
        session->setServerNonce(QString());
        return sendAuthResult(session, envelope, QStringLiteral("rejected"));
    }

    const QJsonValue proofTsValue = envelope.payload.value(QStringLiteral("ts"));
    qint64 proofTs = envelope.ts;
    if (!proofTsValue.isUndefined() && !parseJsonInt64(proofTsValue, &proofTs)) {
        qWarning() << "healthd rejected auth_proof with invalid timestamp for" << envelope.deviceId;
        session->setAuthState(DeviceSession::SessionAuthState::Rejected);
        session->setServerNonce(QString());
        return sendAuthResult(session, envelope, QStringLiteral("rejected"));
    }

    const QByteArray proof = envelope.payload.value(QStringLiteral("proof")).toString().toUtf8().toLower();
    const QString clientNonce = envelope.payload.value(QStringLiteral("client_nonce")).toString();
    const bool verified = stored.info.status == DeviceLifecycleState::Active
        && !stored.secretHash.isEmpty()
        && !clientNonce.isEmpty()
        && authManager_.verify(envelope.deviceId, session->serverNonce(), clientNonce,
            proofTs, stored.secretHash, proof);
    if (verified) {
        session->setAuthState(DeviceSession::SessionAuthState::Active);
        session->setServerNonce(QString());
        qInfo() << "healthd auth success for device" << envelope.deviceId;
        qInfo() << "healthd lifecycle: auth session active"
                << "device_id=" << envelope.deviceId;
        return sendAuthResult(session, envelope, QStringLiteral("ok"));
    }

    session->setAuthState(DeviceSession::SessionAuthState::Rejected);
    session->setServerNonce(QString());
    qWarning() << "healthd auth rejected for device" << envelope.deviceId;
    qWarning() << "healthd lifecycle: auth session rejected"
               << "device_id=" << envelope.deviceId;
    return sendAuthResult(session, envelope, QStringLiteral("rejected"));
}

bool DaemonApp::sendAuthChallenge(
    DeviceSession *session, const DeviceEnvelope &requestEnvelope, const QString &serverNonce) {
    DeviceEnvelope response;
    response.ver = requestEnvelope.ver;
    response.type = QStringLiteral("auth_challenge");
    response.seq = requestEnvelope.seq;
    response.ts = QDateTime::currentSecsSinceEpoch();
    response.deviceId = requestEnvelope.deviceId;
    response.payload.insert(QStringLiteral("server_nonce"), serverNonce);
    return session && session->sendEnvelope(response);
}

bool DaemonApp::sendAuthResult(
    DeviceSession *session, const DeviceEnvelope &requestEnvelope, const QString &result) {
    DeviceEnvelope response;
    response.ver = requestEnvelope.ver;
    response.type = QStringLiteral("auth_result");
    response.seq = requestEnvelope.seq;
    response.ts = QDateTime::currentSecsSinceEpoch();
    response.deviceId = requestEnvelope.deviceId;
    response.payload.insert(QStringLiteral("result"), result);
    return session && session->sendEnvelope(response);
}

bool DaemonApp::parseJsonInt64(const QJsonValue &value, qint64 *out) {
    if (!out || !value.isDouble()) {
        return false;
    }

    const double raw = value.toDouble();
    double integral = 0.0;
    if (!std::isfinite(raw) || std::modf(raw, &integral) != 0.0) {
        return false;
    }

    *out = static_cast<qint64>(integral);
    return true;
}

void DaemonApp::writeTelemetryMarker(const DeviceEnvelope &envelope) {
    if (!isMarkerWritingEnabled() || markerPath_.isEmpty()) {
        return;
    }

    QFile markerFile(markerPath_);
    if (!markerFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "healthd failed to open telemetry marker file" << markerPath_
                   << markerFile.errorString();
        return;
    }

    QJsonObject marker {
        {QStringLiteral("type"), envelope.type},
        {QStringLiteral("device_id"), envelope.deviceId},
        {QStringLiteral("seq"), QString::number(envelope.seq)},
    };
    const QByteArray line = QJsonDocument(marker).toJson(QJsonDocument::Compact);
    if (markerFile.write(line) < 0 || markerFile.write("\n") < 0) {
        qWarning() << "healthd failed to write telemetry marker" << markerPath_
                   << markerFile.errorString();
    }
}

bool DaemonApp::isMarkerWritingEnabled() const {
    // Marker writes are test instrumentation: explicit env var or opt-in control file.
    return markerEnabledByEnv_ || QFile::exists(QString::fromUtf8(kMarkerEnablePath));
}

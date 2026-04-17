#include "storage/database.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {
QString buildConnectionName() {
    return QStringLiteral("rk_health_station_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

qint64 minuteBucket(qint64 sampleTime) {
    if (sampleTime <= 0) {
        sampleTime = QDateTime::currentSecsSinceEpoch();
    }
    return sampleTime - (sampleTime % 60);
}
} // namespace

Database::Database()
    : connectionName_(buildConnectionName()) {
}

Database::~Database() {
    if (!connectionName_.isEmpty()) {
        if (db_.isValid()) {
            db_.close();
        }
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool Database::open(const QString &path, QString *error) {
    if (path.isEmpty()) {
        setError(error, QStringLiteral("database path is empty"));
        return false;
    }

    const QFileInfo fileInfo(path);
    QDir dir;
    if (!dir.mkpath(fileInfo.absolutePath())) {
        setError(error, QStringLiteral("failed to create database directory: %1")
                            .arg(fileInfo.absolutePath()));
        return false;
    }

    if (QSqlDatabase::contains(connectionName_)) {
        db_ = QSqlDatabase::database(connectionName_);
    } else {
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    }

    db_.setDatabaseName(path);
    if (!db_.open()) {
        setError(error, db_.lastError().text());
        return false;
    }

    QSqlQuery pragma(db_);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(error, pragma.lastError().text());
        return false;
    }

    return true;
}

bool Database::initializeSchema(QString *error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }

    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS devices ("
            "device_id TEXT PRIMARY KEY,"
            "device_name TEXT NOT NULL DEFAULT '',"
            "status TEXT NOT NULL DEFAULT 'pending',"
            "secret_hash TEXT NOT NULL DEFAULT '',"
            "bind_mode TEXT NOT NULL DEFAULT '',"
            "firmware_version TEXT NOT NULL DEFAULT '',"
            "hardware_model TEXT NOT NULL DEFAULT '',"
            "last_seen_at INTEGER NOT NULL DEFAULT 0,"
            "last_ip TEXT NOT NULL DEFAULT '',"
            "last_rssi INTEGER NOT NULL DEFAULT 0,"
            "created_at INTEGER NOT NULL,"
            "updated_at INTEGER NOT NULL"
            ")"))) {
        setError(error, query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS telemetry_samples ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "device_id TEXT NOT NULL,"
            "sample_time INTEGER NOT NULL,"
            "heart_rate INTEGER,"
            "spo2 REAL,"
            "acceleration REAL,"
            "finger_detected INTEGER,"
            "battery INTEGER,"
            "rssi INTEGER"
            ")"))) {
        setError(error, query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS device_pending_requests ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "device_id TEXT NOT NULL UNIQUE,"
            "proposed_name TEXT,"
            "firmware_version TEXT,"
            "hardware_model TEXT,"
            "mac TEXT,"
            "ip TEXT,"
            "request_time INTEGER NOT NULL,"
            "status TEXT NOT NULL"
            ")"))) {
        setError(error, query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_telemetry_device_time "
            "ON telemetry_samples(device_id, sample_time)"))) {
        setError(error, query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS device_pending_requests ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "device_id TEXT NOT NULL UNIQUE,"
            "proposed_name TEXT,"
            "firmware_version TEXT,"
            "hardware_model TEXT,"
            "mac TEXT,"
            "ip TEXT,"
            "request_time INTEGER NOT NULL,"
            "status TEXT NOT NULL"
            ")"))) {
        setError(error, query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS device_audit_log ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "device_id TEXT NOT NULL,"
            "action TEXT NOT NULL,"
            "detail TEXT NOT NULL DEFAULT '',"
            "created_at INTEGER NOT NULL"
            ")"))) {
        setError(error, query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS telemetry_minute_agg ("
            "device_id TEXT NOT NULL,"
            "minute_ts INTEGER NOT NULL,"
            "samples_total INTEGER NOT NULL DEFAULT 0,"
            "hr_count INTEGER NOT NULL DEFAULT 0,"
            "hr_min INTEGER,"
            "hr_max INTEGER,"
            "hr_sum INTEGER NOT NULL DEFAULT 0,"
            "spo2_count INTEGER NOT NULL DEFAULT 0,"
            "spo2_min REAL,"
            "spo2_max REAL,"
            "spo2_sum REAL NOT NULL DEFAULT 0,"
            "battery_count INTEGER NOT NULL DEFAULT 0,"
            "battery_min INTEGER,"
            "battery_max INTEGER,"
            "battery_sum INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (device_id, minute_ts)"
            ")"))) {
        setError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool Database::beginTransaction(QString *error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (!db_.transaction()) {
        setError(error, db_.lastError().text());
        return false;
    }
    return true;
}

bool Database::commitTransaction(QString *error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (!db_.commit()) {
        setError(error, db_.lastError().text());
        return false;
    }
    return true;
}

void Database::rollbackTransaction() {
    if (isOpen()) {
        db_.rollback();
    }
}

bool Database::upsertDevice(const DeviceInfo &info, const DeviceStatus &runtime,
    const QString &secretHash, int lastRssi, QString *error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }

    const qint64 now = currentEpochSeconds();
    const qint64 lastSeen = runtime.lastSeenAt;
    const QString status = deviceLifecycleStateToString(info.status);
    const QString secretValue = secretHash.isNull() ? QStringLiteral("") : secretHash;
    const QString bindModeValue = info.bindMode.isNull() ? QStringLiteral("") : info.bindMode;
    const QString firmwareValue
        = info.firmwareVersion.isNull() ? QStringLiteral("") : info.firmwareVersion;
    const QString modelValue = info.model.isNull() ? QStringLiteral("") : info.model;
    const QString remoteIpValue = runtime.remoteIp.isNull() ? QStringLiteral("") : runtime.remoteIp;

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "INSERT INTO devices ("
        "device_id, device_name, status, secret_hash, bind_mode, firmware_version, "
        "hardware_model, last_seen_at, last_ip, last_rssi, created_at, updated_at"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(device_id) DO UPDATE SET "
        "device_name=excluded.device_name, "
        "status=excluded.status, "
        "secret_hash=excluded.secret_hash, "
        "bind_mode=excluded.bind_mode, "
        "firmware_version=excluded.firmware_version, "
        "hardware_model=excluded.hardware_model, "
        "last_seen_at=excluded.last_seen_at, "
        "last_ip=excluded.last_ip, "
        "last_rssi=excluded.last_rssi, "
        "updated_at=excluded.updated_at"));

    query.addBindValue(info.deviceId);
    query.addBindValue(info.deviceName);
    query.addBindValue(status);
    query.addBindValue(secretValue);
    query.addBindValue(bindModeValue);
    query.addBindValue(firmwareValue);
    query.addBindValue(modelValue);
    query.addBindValue(lastSeen);
    query.addBindValue(remoteIpValue);
    query.addBindValue(lastRssi);
    query.addBindValue(now);
    query.addBindValue(now);

    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool Database::fetchDevice(const QString &deviceId, StoredDevice *out, QString *error) const {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (!out) {
        setError(error, QStringLiteral("output pointer is null"));
        return false;
    }

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT device_id, device_name, status, secret_hash, bind_mode, firmware_version, "
        "hardware_model, last_seen_at, last_ip, last_rssi "
        "FROM devices WHERE device_id = ?"));
    query.addBindValue(deviceId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(error, QStringLiteral("device not found: %1").arg(deviceId));
        return false;
    }

    StoredDevice loaded;
    loaded.info.deviceId = query.value(0).toString();
    loaded.info.deviceName = query.value(1).toString();
    loaded.info.status = parseLifecycleState(query.value(2).toString());
    loaded.secretHash = query.value(3).toString();
    loaded.info.deviceSecret = loaded.secretHash;
    loaded.info.bindMode = query.value(4).toString();
    loaded.info.firmwareVersion = query.value(5).toString();
    loaded.info.model = query.value(6).toString();
    loaded.runtime.deviceId = loaded.info.deviceId;
    loaded.runtime.status = loaded.info.status;
    loaded.runtime.lastSeenAt = query.value(7).toLongLong();
    loaded.runtime.online = loaded.runtime.status == DeviceLifecycleState::Active
        && loaded.runtime.lastSeenAt > 0;
    loaded.runtime.remoteIp = query.value(8).toString();
    loaded.lastRssi = query.value(9).toInt();

    *out = loaded;
    return true;
}

QStringList Database::listDeviceIds(QString *error) const {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return {};
    }

    QStringList deviceIds;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT device_id FROM devices ORDER BY device_id ASC"))) {
        setError(error, query.lastError().text());
        return {};
    }

    while (query.next()) {
        deviceIds.append(query.value(0).toString());
    }
    return deviceIds;
}

bool Database::fetchPendingRequest(
    const QString &deviceId, PendingDeviceRequest *out, QString *error) const {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (!out) {
        setError(error, QStringLiteral("output pointer is null"));
        return false;
    }

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT device_id, proposed_name, firmware_version, hardware_model, mac, ip, request_time, status "
        "FROM device_pending_requests WHERE device_id = ?"));
    query.addBindValue(deviceId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(error, QStringLiteral("pending request not found: %1").arg(deviceId));
        return false;
    }

    PendingDeviceRequest request;
    request.deviceId = query.value(0).toString();
    request.proposedName = query.value(1).toString();
    request.firmwareVersion = query.value(2).toString();
    request.hardwareModel = query.value(3).toString();
    request.mac = query.value(4).toString();
    request.ip = query.value(5).toString();
    request.requestTime = query.value(6).toLongLong();
    request.status = query.value(7).toString();
    *out = request;
    return true;
}

QList<Database::PendingDeviceRequest> Database::listPendingRequests(QString *error) const {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return {};
    }

    QList<PendingDeviceRequest> requests;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral(
            "SELECT device_id, proposed_name, firmware_version, hardware_model, mac, ip, request_time, status "
            "FROM device_pending_requests ORDER BY request_time ASC, device_id ASC"))) {
        setError(error, query.lastError().text());
        return {};
    }

    while (query.next()) {
        PendingDeviceRequest request;
        request.deviceId = query.value(0).toString();
        request.proposedName = query.value(1).toString();
        request.firmwareVersion = query.value(2).toString();
        request.hardwareModel = query.value(3).toString();
        request.mac = query.value(4).toString();
        request.ip = query.value(5).toString();
        request.requestTime = query.value(6).toLongLong();
        request.status = query.value(7).toString();
        requests.append(request);
    }

    return requests;
}

bool Database::upsertPendingRequest(const PendingDeviceRequest &request, QString *error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (request.deviceId.isEmpty()) {
        setError(error, QStringLiteral("pending request device_id is empty"));
        return false;
    }

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "INSERT INTO device_pending_requests ("
        "device_id, proposed_name, firmware_version, hardware_model, mac, ip, request_time, status"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(device_id) DO UPDATE SET "
        "proposed_name=excluded.proposed_name, "
        "firmware_version=excluded.firmware_version, "
        "hardware_model=excluded.hardware_model, "
        "mac=excluded.mac, "
        "ip=excluded.ip, "
        "request_time=excluded.request_time, "
        "status=excluded.status"));
    query.addBindValue(request.deviceId);
    query.addBindValue(request.proposedName);
    query.addBindValue(request.firmwareVersion);
    query.addBindValue(request.hardwareModel);
    query.addBindValue(request.mac);
    query.addBindValue(request.ip);
    query.addBindValue(request.requestTime > 0 ? request.requestTime : currentEpochSeconds());
    query.addBindValue(request.status.isEmpty() ? QStringLiteral("pending") : request.status);

    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool Database::deletePendingRequest(const QString &deviceId, QString *error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (deviceId.isEmpty()) {
        setError(error, QStringLiteral("pending request device_id is empty"));
        return false;
    }

    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM device_pending_requests WHERE device_id = ?"));
    query.addBindValue(deviceId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool Database::insertAuditLog(
    const QString &deviceId, const QString &action, const QString &detail, QString *error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (deviceId.isEmpty() || action.isEmpty()) {
        setError(error, QStringLiteral("audit log fields are empty"));
        return false;
    }

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "INSERT INTO device_audit_log (device_id, action, detail, created_at) VALUES (?, ?, ?, ?)"));
    query.addBindValue(deviceId);
    query.addBindValue(action);
    query.addBindValue(detail.isNull() ? QStringLiteral("") : detail);
    query.addBindValue(currentEpochSeconds());
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool Database::insertTelemetrySample(const TelemetryRow &row, QString *error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }

    const qint64 sampleTime = row.sample.sampleTime > 0 ? row.sample.sampleTime : currentEpochSeconds();

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "INSERT INTO telemetry_samples ("
        "device_id, sample_time, heart_rate, spo2, acceleration, finger_detected, battery, rssi"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(row.sample.deviceId);
    query.addBindValue(sampleTime);
    query.addBindValue(row.heartRate);
    query.addBindValue(row.spo2);
    query.addBindValue(row.acceleration);
    query.addBindValue(row.fingerDetected);
    query.addBindValue(row.battery);
    query.addBindValue(row.rssi);

    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool Database::fetchLatestTelemetry(
    const QString &deviceId, LatestTelemetryRecord *out, QString *error) const {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (deviceId.isEmpty()) {
        setError(error, QStringLiteral("latest telemetry device_id is empty"));
        return false;
    }
    if (!out) {
        setError(error, QStringLiteral("latest telemetry output pointer is null"));
        return false;
    }

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT sample_time, heart_rate, spo2, battery "
        "FROM telemetry_samples WHERE device_id = ? "
        "ORDER BY sample_time DESC, id DESC LIMIT 1"));
    query.addBindValue(deviceId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(error, QStringLiteral("latest telemetry not found: %1").arg(deviceId));
        return false;
    }

    LatestTelemetryRecord record;
    record.sample.deviceId = deviceId;
    record.sample.sampleTime = query.value(0).toLongLong();
    record.heartRate = query.value(1);
    record.spo2 = query.value(2);
    record.battery = query.value(3);
    *out = record;
    return true;
}

bool Database::upsertTelemetryMinuteAgg(const TelemetryRow &row, QString *error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (row.sample.deviceId.isEmpty()) {
        setError(error, QStringLiteral("telemetry minute agg device_id is empty"));
        return false;
    }

    const qint64 bucketTs = minuteBucket(row.sample.sampleTime);
    const bool hasHeartRate = row.heartRate.isValid() && !row.heartRate.isNull();
    const bool hasSpo2 = row.spo2.isValid() && !row.spo2.isNull();
    const bool hasBattery = row.battery.isValid() && !row.battery.isNull();

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "INSERT INTO telemetry_minute_agg ("
        "device_id, minute_ts, samples_total, "
        "hr_count, hr_min, hr_max, hr_sum, "
        "spo2_count, spo2_min, spo2_max, spo2_sum, "
        "battery_count, battery_min, battery_max, battery_sum"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(device_id, minute_ts) DO UPDATE SET "
        "samples_total = telemetry_minute_agg.samples_total + excluded.samples_total, "
        "hr_count = telemetry_minute_agg.hr_count + excluded.hr_count, "
        "hr_min = CASE "
        "WHEN excluded.hr_count = 0 THEN telemetry_minute_agg.hr_min "
        "WHEN telemetry_minute_agg.hr_count = 0 OR telemetry_minute_agg.hr_min IS NULL THEN excluded.hr_min "
        "ELSE MIN(telemetry_minute_agg.hr_min, excluded.hr_min) END, "
        "hr_max = CASE "
        "WHEN excluded.hr_count = 0 THEN telemetry_minute_agg.hr_max "
        "WHEN telemetry_minute_agg.hr_count = 0 OR telemetry_minute_agg.hr_max IS NULL THEN excluded.hr_max "
        "ELSE MAX(telemetry_minute_agg.hr_max, excluded.hr_max) END, "
        "hr_sum = telemetry_minute_agg.hr_sum + excluded.hr_sum, "
        "spo2_count = telemetry_minute_agg.spo2_count + excluded.spo2_count, "
        "spo2_min = CASE "
        "WHEN excluded.spo2_count = 0 THEN telemetry_minute_agg.spo2_min "
        "WHEN telemetry_minute_agg.spo2_count = 0 OR telemetry_minute_agg.spo2_min IS NULL THEN excluded.spo2_min "
        "ELSE MIN(telemetry_minute_agg.spo2_min, excluded.spo2_min) END, "
        "spo2_max = CASE "
        "WHEN excluded.spo2_count = 0 THEN telemetry_minute_agg.spo2_max "
        "WHEN telemetry_minute_agg.spo2_count = 0 OR telemetry_minute_agg.spo2_max IS NULL THEN excluded.spo2_max "
        "ELSE MAX(telemetry_minute_agg.spo2_max, excluded.spo2_max) END, "
        "spo2_sum = telemetry_minute_agg.spo2_sum + excluded.spo2_sum, "
        "battery_count = telemetry_minute_agg.battery_count + excluded.battery_count, "
        "battery_min = CASE "
        "WHEN excluded.battery_count = 0 THEN telemetry_minute_agg.battery_min "
        "WHEN telemetry_minute_agg.battery_count = 0 OR telemetry_minute_agg.battery_min IS NULL THEN excluded.battery_min "
        "ELSE MIN(telemetry_minute_agg.battery_min, excluded.battery_min) END, "
        "battery_max = CASE "
        "WHEN excluded.battery_count = 0 THEN telemetry_minute_agg.battery_max "
        "WHEN telemetry_minute_agg.battery_count = 0 OR telemetry_minute_agg.battery_max IS NULL THEN excluded.battery_max "
        "ELSE MAX(telemetry_minute_agg.battery_max, excluded.battery_max) END, "
        "battery_sum = telemetry_minute_agg.battery_sum + excluded.battery_sum"));
    query.addBindValue(row.sample.deviceId);
    query.addBindValue(bucketTs);
    query.addBindValue(1);
    query.addBindValue(hasHeartRate ? 1 : 0);
    query.addBindValue(hasHeartRate ? row.heartRate : QVariant(QVariant::Int));
    query.addBindValue(hasHeartRate ? row.heartRate : QVariant(QVariant::Int));
    query.addBindValue(hasHeartRate ? row.heartRate.toLongLong() : 0);
    query.addBindValue(hasSpo2 ? 1 : 0);
    query.addBindValue(hasSpo2 ? row.spo2 : QVariant(QVariant::Double));
    query.addBindValue(hasSpo2 ? row.spo2 : QVariant(QVariant::Double));
    query.addBindValue(hasSpo2 ? row.spo2.toDouble() : 0.0);
    query.addBindValue(hasBattery ? 1 : 0);
    query.addBindValue(hasBattery ? row.battery : QVariant(QVariant::Int));
    query.addBindValue(hasBattery ? row.battery : QVariant(QVariant::Int));
    query.addBindValue(hasBattery ? row.battery.toLongLong() : 0);

    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }

    return true;
}

QList<Database::TelemetryMinuteAggRow> Database::fetchTelemetryMinuteAgg(
    const QString &deviceId, qint64 fromTs, qint64 toTs, QString *error) const {
    if (!isOpen()) {
        setError(error, QStringLiteral("database is not open"));
        return {};
    }
    if (deviceId.isEmpty()) {
        setError(error, QStringLiteral("telemetry minute agg device_id is empty"));
        return {};
    }

    QList<TelemetryMinuteAggRow> rows;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT device_id, minute_ts, samples_total, "
        "hr_count, hr_min, hr_max, hr_sum, "
        "spo2_count, spo2_min, spo2_max, spo2_sum, "
        "battery_count, battery_min, battery_max, battery_sum "
        "FROM telemetry_minute_agg "
        "WHERE device_id = ? AND minute_ts >= ? AND minute_ts <= ? "
        "ORDER BY minute_ts ASC"));
    query.addBindValue(deviceId);
    query.addBindValue(fromTs);
    query.addBindValue(toTs);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return {};
    }

    while (query.next()) {
        TelemetryMinuteAggRow row;
        row.deviceId = query.value(0).toString();
        row.minuteTs = query.value(1).toLongLong();
        row.samplesTotal = query.value(2).toInt();
        row.hrCount = query.value(3).toInt();
        row.hrMin = query.value(4);
        row.hrMax = query.value(5);
        row.hrSum = query.value(6).toLongLong();
        row.spo2Count = query.value(7).toInt();
        row.spo2Min = query.value(8);
        row.spo2Max = query.value(9);
        row.spo2Sum = query.value(10).toDouble();
        row.batteryCount = query.value(11).toInt();
        row.batteryMin = query.value(12);
        row.batteryMax = query.value(13);
        row.batterySum = query.value(14).toLongLong();
        rows.append(row);
    }

    return rows;
}

bool Database::isOpen() const {
    return db_.isValid() && db_.isOpen();
}

DeviceLifecycleState Database::parseLifecycleState(const QString &value) {
    if (value == QStringLiteral("active")) {
        return DeviceLifecycleState::Active;
    }
    if (value == QStringLiteral("disabled")) {
        return DeviceLifecycleState::Disabled;
    }
    if (value == QStringLiteral("revoked")) {
        return DeviceLifecycleState::Revoked;
    }
    if (value == QStringLiteral("offline")) {
        return DeviceLifecycleState::Offline;
    }
    return DeviceLifecycleState::Pending;
}

qint64 Database::currentEpochSeconds() {
    return QDateTime::currentSecsSinceEpoch();
}

void Database::setError(QString *error, const QString &message) {
    if (error) {
        *error = message;
    }
}

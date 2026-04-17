#include "core/video_storage_service.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

QString VideoStorageService::normalizeDir(const QString &candidate) const {
    const QString trimmed = candidate.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    QDir dir(trimmed);
    QString normalized = dir.absolutePath();
    if (!normalized.endsWith(QDir::separator())) {
        normalized.append(QDir::separator());
    }
    return normalized;
}

bool VideoStorageService::ensureWritableDirectory(const QString &path, QString *errorCode) const {
    if (errorCode) {
        errorCode->clear();
    }

    if (path.isEmpty()) {
        if (errorCode) {
            *errorCode = QStringLiteral("storage_dir_invalid");
        }
        return false;
    }

    QDir dir(path);
    if (!dir.exists() && !QDir().mkpath(path)) {
        if (errorCode) {
            *errorCode = QStringLiteral("storage_dir_invalid");
        }
        return false;
    }

    const QFileInfo info(path);
    if (!info.isDir() || !info.isWritable()) {
        if (errorCode) {
            *errorCode = QStringLiteral("storage_dir_invalid");
        }
        return false;
    }

    const QString probePath = QDir(path).filePath(QStringLiteral(".rk_video_write_probe"));
    QFile probeFile(probePath);
    if (!probeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorCode) {
            *errorCode = QStringLiteral("storage_dir_invalid");
        }
        return false;
    }
    probeFile.close();
    probeFile.remove();
    return true;
}

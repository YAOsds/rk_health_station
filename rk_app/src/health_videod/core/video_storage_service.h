#pragma once

#include <QString>

class VideoStorageService {
public:
    QString normalizeDir(const QString &candidate) const;
    bool ensureWritableDirectory(const QString &path, QString *errorCode) const;
};

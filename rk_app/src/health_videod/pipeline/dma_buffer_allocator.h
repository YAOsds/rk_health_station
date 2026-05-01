#pragma once

#include <QByteArray>
#include <QString>

class DmaBufferAllocator {
public:
    int allocate(const QString &heapPath, int bytes, QString *error) const;
    bool writePayload(int fd, const QByteArray &payload, QString *error) const;
};

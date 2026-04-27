#pragma once

#include <QString>

bool sendFileDescriptor(int socketFd, int fdToSend, QString *error = nullptr);
int receiveFileDescriptor(int socketFd, QString *error = nullptr);

int connectUnixStreamSocket(const QString &path, QString *error = nullptr);
int createUnixStreamServerSocket(const QString &path, QString *error = nullptr);
int acceptUnixStreamClient(int serverFd, QString *error = nullptr);
void removeUnixStreamSocket(const QString &path);

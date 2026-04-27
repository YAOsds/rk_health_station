#pragma once

#include <QString>

bool sendFileDescriptor(int socketFd, int fdToSend, QString *error = nullptr);
int receiveFileDescriptor(int socketFd, QString *error = nullptr);

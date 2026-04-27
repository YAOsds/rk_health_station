#include "protocol/unix_fd_passing.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace {
void setError(QString *error, const QString &message) {
    if (error) {
        *error = message;
    }
}

QString errnoMessage(const char *prefix) {
    return QStringLiteral("%1_%2_%3").arg(QString::fromLatin1(prefix)).arg(errno).arg(QString::fromLocal8Bit(strerror(errno)));
}
}

bool sendFileDescriptor(int socketFd, int fdToSend, QString *error) {
    if (socketFd < 0 || fdToSend < 0) {
        setError(error, QStringLiteral("fd_passing_invalid_fd"));
        return false;
    }

    char payload = 'F';
    iovec iov{};
    iov.iov_base = &payload;
    iov.iov_len = sizeof(payload);

    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))] = {};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fdToSend, sizeof(int));

    if (::sendmsg(socketFd, &msg, 0) != static_cast<ssize_t>(sizeof(payload))) {
        setError(error, errnoMessage("fd_passing_sendmsg_failed"));
        return false;
    }

    setError(error, QString());
    return true;
}

int receiveFileDescriptor(int socketFd, QString *error) {
    if (socketFd < 0) {
        setError(error, QStringLiteral("fd_passing_invalid_socket"));
        return -1;
    }

    char payload = 0;
    iovec iov{};
    iov.iov_base = &payload;
    iov.iov_len = sizeof(payload);

    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))] = {};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

#ifdef MSG_CMSG_CLOEXEC
    const int flags = MSG_CMSG_CLOEXEC;
#else
    const int flags = 0;
#endif
    const ssize_t bytes = ::recvmsg(socketFd, &msg, flags);
    if (bytes <= 0) {
        setError(error, errnoMessage("fd_passing_recvmsg_failed"));
        return -1;
    }

    for (cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS
            && cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
            int receivedFd = -1;
            memcpy(&receivedFd, CMSG_DATA(cmsg), sizeof(int));
            setError(error, QString());
            return receivedFd;
        }
    }

    setError(error, QStringLiteral("fd_passing_missing_descriptor"));
    return -1;
}

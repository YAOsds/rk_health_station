#include "protocol/unix_fd_passing.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <fcntl.h>
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

namespace {
bool fillUnixAddress(const QString &path, sockaddr_un *address, socklen_t *length, QString *error) {
    const QByteArray encoded = path.toUtf8();
    if (encoded.isEmpty() || encoded.size() >= static_cast<int>(sizeof(address->sun_path))) {
        setError(error, QStringLiteral("unix_socket_path_invalid"));
        return false;
    }

    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    memcpy(address->sun_path, encoded.constData(), static_cast<size_t>(encoded.size() + 1));
    *length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + encoded.size() + 1);
    return true;
}
}

int connectUnixStreamSocket(const QString &path, QString *error) {
    sockaddr_un address{};
    socklen_t length = 0;
    if (!fillUnixAddress(path, &address, &length, error)) {
        return -1;
    }

#ifdef SOCK_CLOEXEC
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
#endif
    if (fd < 0) {
        setError(error, errnoMessage("unix_socket_create_failed"));
        return -1;
    }
#ifndef SOCK_CLOEXEC
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    if (::connect(fd, reinterpret_cast<sockaddr *>(&address), length) != 0) {
        setError(error, errnoMessage("unix_socket_connect_failed"));
        ::close(fd);
        return -1;
    }

    setError(error, QString());
    return fd;
}

int createUnixStreamServerSocket(const QString &path, QString *error) {
    sockaddr_un address{};
    socklen_t length = 0;
    if (!fillUnixAddress(path, &address, &length, error)) {
        return -1;
    }

    ::unlink(path.toUtf8().constData());
#ifdef SOCK_CLOEXEC
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
#endif
    if (fd < 0) {
        setError(error, errnoMessage("unix_socket_create_failed"));
        return -1;
    }
#ifndef SOCK_CLOEXEC
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), length) != 0) {
        setError(error, errnoMessage("unix_socket_bind_failed"));
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 8) != 0) {
        setError(error, errnoMessage("unix_socket_listen_failed"));
        ::close(fd);
        return -1;
    }

    setError(error, QString());
    return fd;
}

int acceptUnixStreamClient(int serverFd, QString *error) {
    if (serverFd < 0) {
        setError(error, QStringLiteral("unix_socket_invalid_server_fd"));
        return -1;
    }
#ifdef SOCK_CLOEXEC
    const int clientFd = ::accept4(serverFd, nullptr, nullptr, SOCK_CLOEXEC);
#else
    const int clientFd = ::accept(serverFd, nullptr, nullptr);
#endif
    if (clientFd < 0) {
        setError(error, errnoMessage("unix_socket_accept_failed"));
        return -1;
    }
#ifndef SOCK_CLOEXEC
    ::fcntl(clientFd, F_SETFD, FD_CLOEXEC);
#endif
    setError(error, QString());
    return clientFd;
}

void removeUnixStreamSocket(const QString &path) {
    if (!path.isEmpty()) {
        ::unlink(path.toUtf8().constData());
    }
}

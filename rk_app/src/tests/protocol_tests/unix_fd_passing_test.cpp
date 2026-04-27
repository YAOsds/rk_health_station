#include "protocol/unix_fd_passing.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

class UnixFdPassingTest : public QObject {
    Q_OBJECT

private slots:
    void passesReadableFileDescriptorOverSocketPair();
};

void UnixFdPassingTest::passesReadableFileDescriptorOverSocketPair() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString path = tempDir.filePath(QStringLiteral("payload.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("dmabuf-fd-payload");
    file.close();

    const int payloadFd = ::open(path.toUtf8().constData(), O_RDONLY | O_CLOEXEC);
    QVERIFY(payloadFd >= 0);

    int sockets[2] = {-1, -1};
    QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) == 0);

    QString error;
    QVERIFY2(sendFileDescriptor(sockets[0], payloadFd, &error), qPrintable(error));
    const int receivedFd = receiveFileDescriptor(sockets[1], &error);
    QVERIFY2(receivedFd >= 0, qPrintable(error));

    char buffer[64] = {};
    const ssize_t bytesRead = ::read(receivedFd, buffer, sizeof(buffer));
    QCOMPARE(bytesRead, static_cast<ssize_t>(17));
    QCOMPARE(QByteArray(buffer, static_cast<int>(bytesRead)), QByteArray("dmabuf-fd-payload"));

    ::close(receivedFd);
    ::close(payloadFd);
    ::close(sockets[0]);
    ::close(sockets[1]);
}

QTEST_MAIN(UnixFdPassingTest)
#include "unix_fd_passing_test.moc"

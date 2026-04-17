#include "ipc_client/fall_ipc_client.h"

#include "protocol/fall_ipc.h"

#include <QJsonDocument>
#include <QLocalSocket>

namespace {
const char kSocketName[] = "rk_fall.sock";
const char kSocketEnvVar[] = "RK_FALL_SOCKET_NAME";
}

FallIpcClient::FallIpcClient(const QString &socketName, QObject *parent)
    : AbstractFallClient(parent)
    , socketName_(socketName)
    , socket_(new QLocalSocket(this)) {
    qRegisterMetaType<FallClassificationResult>("FallClassificationResult");
    qRegisterMetaType<FallClassificationBatch>("FallClassificationBatch");
    connect(socket_, &QLocalSocket::readyRead, this, [this]() { onReadyRead(); });
    connect(socket_, &QLocalSocket::connected, this, [this]() {
        emit connectionChanged(true);
    });
    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
        emit connectionChanged(false);
    });
}

bool FallIpcClient::connectToBackend() {
    if (socket_->state() == QLocalSocket::ConnectedState) {
        return true;
    }

    const QString envSocketName = qEnvironmentVariable(kSocketEnvVar);
    const QString resolvedSocketName = !socketName_.isEmpty()
            ? socketName_
            : (!envSocketName.isEmpty() ? envSocketName : QString::fromUtf8(kSocketName));

    socket_->abort();
    socket_->connectToServer(resolvedSocketName);
    if (!socket_->waitForConnected(2000)) {
        emit errorOccurred(socket_->errorString());
        return false;
    }

    socket_->write("{\"action\":\"subscribe_classification\"}\n");
    socket_->flush();
    return true;
}

void FallIpcClient::disconnectFromBackend() {
    socket_->abort();
    readBuffer_.clear();
}

void FallIpcClient::onReadyRead() {
    readBuffer_.append(socket_->readAll());
    while (true) {
        const int newlineIndex = readBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        const QByteArray line = readBuffer_.left(newlineIndex).trimmed();
        readBuffer_.remove(0, newlineIndex + 1);
        if (line.isEmpty()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(line);
        FallClassificationBatch batch;
        FallClassificationResult result;
        if (document.isObject() && fallClassificationBatchFromJson(document.object(), &batch)) {
            emit classificationBatchUpdated(batch);
        } else if (document.isObject()
            && fallClassificationResultFromJson(document.object(), &result)) {
            emit classificationUpdated(result);
        }
    }
}

#pragma once

#include "models/fall_models.h"

#include <QObject>
#include <QPointer>
#include <QVector>

class QLocalServer;
class QLocalSocket;

class FallGateway : public QObject {
    Q_OBJECT

public:
    explicit FallGateway(const FallRuntimeStatus &initialStatus, QObject *parent = nullptr);
    ~FallGateway() override;

    bool start();
    void stop();
    void setRuntimeStatus(const FallRuntimeStatus &status);
    void setSocketName(const QString &socketName);
    void publishClassification(const FallClassificationResult &result);
    void publishEvent(const FallEvent &event);

private:
    void onNewConnection();
    void onSocketReadyRead();
    void removeSubscriber(QLocalSocket *socket);
    QByteArray buildStatusResponse() const;
    QByteArray buildClassificationMessage(const FallClassificationResult &result) const;
    QByteArray buildEventMessage(const FallEvent &event) const;

    FallRuntimeStatus status_;
    QLocalServer *server_ = nullptr;
    QString socketName_ = QStringLiteral("rk_fall.sock");
    QVector<QPointer<QLocalSocket>> classificationSubscribers_;
};

#pragma once

#include "models/fall_models.h"

#include <QObject>

class QLocalSocket;

class AbstractFallClient : public QObject {
    Q_OBJECT

public:
    explicit AbstractFallClient(QObject *parent = nullptr)
        : QObject(parent) {
    }
    ~AbstractFallClient() override = default;

    virtual bool connectToBackend() = 0;
    virtual void disconnectFromBackend() = 0;

signals:
    void classificationUpdated(const FallClassificationResult &result);
    void classificationBatchUpdated(const FallClassificationBatch &batch);
    void connectionChanged(bool connected);
    void errorOccurred(const QString &errorText);
};

class FallIpcClient : public AbstractFallClient {
    Q_OBJECT

public:
    explicit FallIpcClient(
        const QString &socketName = QString(),
        QObject *parent = nullptr);

    bool connectToBackend() override;
    void disconnectFromBackend() override;

private:
    void onReadyRead();

    QString socketName_;
    QLocalSocket *socket_ = nullptr;
    QByteArray readBuffer_;
};

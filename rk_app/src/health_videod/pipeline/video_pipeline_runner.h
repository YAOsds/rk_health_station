#pragma once

#include <QObject>

class QString;
struct PipelineSession;

class VideoPipelineRunner : public QObject {
public:
    explicit VideoPipelineRunner(QObject *parent = nullptr)
        : QObject(parent) {
    }

    ~VideoPipelineRunner() override = default;

    virtual bool startPreview(PipelineSession &session, QString *error) = 0;
    virtual bool stopPreview(PipelineSession &session, QString *error) = 0;
};

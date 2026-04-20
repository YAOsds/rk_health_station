#include "tracking/track_trace_logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QString trackStateToString(ByteTrackState state) {
    switch (state) {
    case ByteTrackState::Tracked:
        return QStringLiteral("tracked");
    case ByteTrackState::Lost:
        return QStringLiteral("lost");
    case ByteTrackState::Removed:
        return QStringLiteral("removed");
    }
    return QStringLiteral("unknown");
}

QJsonObject rectToJson(const QRectF &rect) {
    QJsonObject box;
    box.insert(QStringLiteral("x"), rect.x());
    box.insert(QStringLiteral("y"), rect.y());
    box.insert(QStringLiteral("w"), rect.width());
    box.insert(QStringLiteral("h"), rect.height());
    return box;
}
}

TrackTraceLogger::TrackTraceLogger(const QString &path)
    : path_(path) {
}

bool TrackTraceLogger::isEnabled() const {
    return !path_.isEmpty();
}

void TrackTraceLogger::appendFrame(const AnalysisFramePacket &frame, qint64 inferTs,
    const QString &cameraId, const QVector<TrackedPerson> &tracks,
    const QVector<TrackTraceEvent> &events) const {
    if (!isEnabled()) {
        return;
    }

    const QFileInfo fileInfo(path_);
    if (fileInfo.dir().exists() || QDir().mkpath(fileInfo.dir().absolutePath())) {
        QJsonArray tracksJson;
        for (const TrackedPerson &track : tracks) {
            QJsonObject trackJson;
            trackJson.insert(QStringLiteral("track_id"), track.trackId);
            trackJson.insert(QStringLiteral("state"), trackStateToString(track.state));
            trackJson.insert(QStringLiteral("bbox"), rectToJson(track.latestPose.box));
            trackJson.insert(QStringLiteral("pose_score"), track.latestPose.score);
            trackJson.insert(QStringLiteral("last_classification_state"), track.lastClassificationState);
            trackJson.insert(
                QStringLiteral("last_classification_confidence"), track.lastClassificationConfidence);
            trackJson.insert(QStringLiteral("has_fresh_classification"), track.hasFreshClassification);
            trackJson.insert(QStringLiteral("sequence_size"), track.action.sequence.values().size());
            trackJson.insert(QStringLiteral("sequence_full"), track.action.sequence.isFull());
            tracksJson.append(trackJson);
        }

        QJsonArray eventsJson;
        for (const TrackTraceEvent &event : events) {
            QJsonObject eventJson;
            eventJson.insert(QStringLiteral("track_id"), event.trackId);
            eventJson.insert(QStringLiteral("event_type"), event.eventType);
            eventJson.insert(QStringLiteral("confidence"), event.confidence);
            eventsJson.append(eventJson);
        }

        QJsonObject root;
        root.insert(QStringLiteral("frame_id"), static_cast<qint64>(frame.frameId));
        root.insert(QStringLiteral("frame_ts"), frame.timestampMs);
        root.insert(QStringLiteral("infer_ts"), inferTs);
        root.insert(QStringLiteral("camera_id"), cameraId);
        root.insert(QStringLiteral("tracks"), tracksJson);
        root.insert(QStringLiteral("events"), eventsJson);

        QFile file(path_);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            return;
        }
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.write("\n");
    }
}

#include "protocol/fall_ipc.h"

#include <QJsonArray>

QJsonObject fallRuntimeStatusToJson(const FallRuntimeStatus &status) {
    QJsonObject json;
    json.insert(QStringLiteral("camera_id"), status.cameraId);
    json.insert(QStringLiteral("input_connected"), status.inputConnected);
    json.insert(QStringLiteral("pose_model_ready"), status.poseModelReady);
    json.insert(QStringLiteral("action_model_ready"), status.actionModelReady);
    json.insert(QStringLiteral("current_fps"), status.currentFps);
    json.insert(QStringLiteral("last_frame_ts"), static_cast<qint64>(status.lastFrameTs));
    json.insert(QStringLiteral("last_infer_ts"), static_cast<qint64>(status.lastInferTs));
    json.insert(QStringLiteral("latest_state"), status.latestState);
    json.insert(QStringLiteral("latest_confidence"), status.latestConfidence);
    json.insert(QStringLiteral("last_error"), status.lastError);
    return json;
}

bool fallRuntimeStatusFromJson(const QJsonObject &json, FallRuntimeStatus *status) {
    if (!status) {
        return false;
    }

    status->cameraId = json.value(QStringLiteral("camera_id")).toString();
    status->inputConnected = json.value(QStringLiteral("input_connected")).toBool();
    status->poseModelReady = json.value(QStringLiteral("pose_model_ready")).toBool();
    status->actionModelReady = json.value(QStringLiteral("action_model_ready")).toBool();
    status->currentFps = json.value(QStringLiteral("current_fps")).toDouble();
    status->lastFrameTs = static_cast<qint64>(json.value(QStringLiteral("last_frame_ts")).toDouble());
    status->lastInferTs = static_cast<qint64>(json.value(QStringLiteral("last_infer_ts")).toDouble());
    status->latestState = json.value(QStringLiteral("latest_state")).toString();
    status->latestConfidence = json.value(QStringLiteral("latest_confidence")).toDouble();
    status->lastError = json.value(QStringLiteral("last_error")).toString();
    return !status->cameraId.isEmpty();
}

QJsonObject fallClassificationResultToJson(const FallClassificationResult &result) {
    QJsonObject json;
    json.insert(QStringLiteral("type"), QStringLiteral("classification"));
    json.insert(QStringLiteral("camera_id"), result.cameraId);
    json.insert(QStringLiteral("ts"), static_cast<qint64>(result.timestampMs));
    json.insert(QStringLiteral("state"), result.state);
    json.insert(QStringLiteral("confidence"), result.confidence);
    return json;
}

bool fallClassificationResultFromJson(const QJsonObject &json, FallClassificationResult *result) {
    if (!result) {
        return false;
    }

    result->cameraId = json.value(QStringLiteral("camera_id")).toString();
    result->timestampMs = static_cast<qint64>(json.value(QStringLiteral("ts")).toDouble());
    result->state = json.value(QStringLiteral("state")).toString();
    result->confidence = json.value(QStringLiteral("confidence")).toDouble();
    return json.value(QStringLiteral("type")).toString() == QStringLiteral("classification")
        && !result->cameraId.isEmpty()
        && !result->state.isEmpty();
}

QJsonObject fallClassificationBatchToJson(const FallClassificationBatch &batch) {
    QJsonArray results;
    for (const FallClassificationEntry &entry : batch.results) {
        QJsonObject item;
        item.insert(QStringLiteral("state"), entry.state);
        item.insert(QStringLiteral("confidence"), entry.confidence);
        results.append(item);
    }

    QJsonObject json;
    json.insert(QStringLiteral("type"), QStringLiteral("classification_batch"));
    json.insert(QStringLiteral("camera_id"), batch.cameraId);
    json.insert(QStringLiteral("ts"), static_cast<qint64>(batch.timestampMs));
    json.insert(QStringLiteral("person_count"), batch.results.size());
    json.insert(QStringLiteral("results"), results);
    return json;
}

bool fallClassificationBatchFromJson(const QJsonObject &json, FallClassificationBatch *batch) {
    if (!batch) {
        return false;
    }

    batch->cameraId = json.value(QStringLiteral("camera_id")).toString();
    batch->timestampMs = static_cast<qint64>(json.value(QStringLiteral("ts")).toDouble());
    batch->results.clear();

    const QJsonArray results = json.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject object = value.toObject();
        FallClassificationEntry entry;
        entry.state = object.value(QStringLiteral("state")).toString();
        entry.confidence = object.value(QStringLiteral("confidence")).toDouble();
        if (entry.state.isEmpty()) {
            return false;
        }
        batch->results.push_back(entry);
    }

    return json.value(QStringLiteral("type")).toString() == QStringLiteral("classification_batch")
        && !batch->cameraId.isEmpty()
        && json.value(QStringLiteral("person_count")).toInt() == batch->results.size();
}

QJsonObject fallEventToJson(const FallEvent &event) {
    QJsonObject json;
    json.insert(QStringLiteral("event_id"), event.eventId);
    json.insert(QStringLiteral("camera_id"), event.cameraId);
    json.insert(QStringLiteral("ts_start"), static_cast<qint64>(event.tsStart));
    json.insert(QStringLiteral("ts_confirm"), static_cast<qint64>(event.tsConfirm));
    json.insert(QStringLiteral("event_type"), event.eventType);
    json.insert(QStringLiteral("confidence"), event.confidence);
    json.insert(QStringLiteral("snapshot_ref"), event.snapshotRef);
    json.insert(QStringLiteral("clip_ref"), event.clipRef);
    return json;
}

bool fallEventFromJson(const QJsonObject &json, FallEvent *event) {
    if (!event) {
        return false;
    }

    event->eventId = json.value(QStringLiteral("event_id")).toString();
    event->cameraId = json.value(QStringLiteral("camera_id")).toString();
    event->tsStart = static_cast<qint64>(json.value(QStringLiteral("ts_start")).toDouble());
    event->tsConfirm = static_cast<qint64>(json.value(QStringLiteral("ts_confirm")).toDouble());
    event->eventType = json.value(QStringLiteral("event_type")).toString();
    event->confidence = json.value(QStringLiteral("confidence")).toDouble();
    event->snapshotRef = json.value(QStringLiteral("snapshot_ref")).toString();
    event->clipRef = json.value(QStringLiteral("clip_ref")).toString();
    return !event->cameraId.isEmpty() && !event->eventType.isEmpty();
}

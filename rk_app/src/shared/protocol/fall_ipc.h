#pragma once

#include "models/fall_models.h"

#include <QJsonObject>

QJsonObject fallRuntimeStatusToJson(const FallRuntimeStatus &status);
bool fallRuntimeStatusFromJson(const QJsonObject &json, FallRuntimeStatus *status);
QJsonObject fallClassificationResultToJson(const FallClassificationResult &result);
bool fallClassificationResultFromJson(const QJsonObject &json, FallClassificationResult *result);
QJsonObject fallClassificationBatchToJson(const FallClassificationBatch &batch);
bool fallClassificationBatchFromJson(const QJsonObject &json, FallClassificationBatch *batch);
QJsonObject fallEventToJson(const FallEvent &event);
bool fallEventFromJson(const QJsonObject &json, FallEvent *event);

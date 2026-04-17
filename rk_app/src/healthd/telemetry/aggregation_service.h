#pragma once

#include "storage/database.h"

#include <QString>

class AggregationService {
public:
    explicit AggregationService(Database *database = nullptr);

    void setDatabase(Database *database);
    bool recordTelemetry(const Database::TelemetryRow &row, QString *error = nullptr) const;

private:
    Database *database_ = nullptr;
};

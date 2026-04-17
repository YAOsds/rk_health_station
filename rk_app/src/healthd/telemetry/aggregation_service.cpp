#include "telemetry/aggregation_service.h"

#include "storage/database.h"

AggregationService::AggregationService(Database *database)
    : database_(database) {
}

void AggregationService::setDatabase(Database *database) {
    database_ = database;
}

bool AggregationService::recordTelemetry(const Database::TelemetryRow &row, QString *error) const {
    if (!database_) {
        return true;
    }
    return database_->upsertTelemetryMinuteAgg(row, error);
}

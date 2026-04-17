#include "domain/fall_event_policy.h"

std::optional<FallEvent> FallEventPolicy::update(const QString &rawState, double confidence) {
    if (rawState == QStringLiteral("fall") || rawState == QStringLiteral("lie")) {
        ++fallLikeCount_;
    } else {
        fallLikeCount_ = 0;
    }

    if (fallLikeCount_ < 3) {
        return std::nullopt;
    }

    FallEvent event;
    event.eventType = QStringLiteral("fall_confirmed");
    event.confidence = confidence;
    return event;
}

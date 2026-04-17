#include "domain/fall_event_policy.h"

std::optional<FallEvent> FallEventPolicy::update(const QString &rawState, double confidence) {
    if (rawState == QStringLiteral("fall") || rawState == QStringLiteral("lie")) {
        ++fallLikeCount_;
    } else {
        fallLikeCount_ = 0;
        eventEmittedForCurrentEpisode_ = false;
    }

    if (fallLikeCount_ < 3 || eventEmittedForCurrentEpisode_) {
        return std::nullopt;
    }

    eventEmittedForCurrentEpisode_ = true;
    FallEvent event;
    event.eventType = QStringLiteral("fall_confirmed");
    event.confidence = confidence;
    return event;
}

void FallEventPolicy::reset() {
    fallLikeCount_ = 0;
    eventEmittedForCurrentEpisode_ = false;
}

int FallEventPolicy::fallLikeCount() const {
    return fallLikeCount_;
}

#include "tracking/track_icon_registry.h"

TrackIconRegistry::TrackIconRegistry(int maxIcons)
    : maxIcons_(maxIcons) {
}

int TrackIconRegistry::iconIdForTrack(int trackId) {
    if (trackToIcon_.contains(trackId)) {
        return trackToIcon_.value(trackId);
    }

    const int iconId = takeNextIconId();
    trackToIcon_.insert(trackId, iconId);
    usedIcons_.insert(iconId);
    return iconId;
}

void TrackIconRegistry::reconcileActiveTracks(const QVector<int> &activeTrackIds) {
    const QSet<int> activeSet(activeTrackIds.cbegin(), activeTrackIds.cend());
    auto it = trackToIcon_.begin();
    while (it != trackToIcon_.end()) {
        if (!activeSet.contains(it.key())) {
            usedIcons_.remove(it.value());
            it = trackToIcon_.erase(it);
            continue;
        }
        ++it;
    }
}

int TrackIconRegistry::takeNextIconId() {
    for (int iconId = 1; iconId <= maxIcons_; ++iconId) {
        if (!usedIcons_.contains(iconId)) {
            return iconId;
        }
    }
    return maxIcons_ > 0 ? maxIcons_ : 1;
}

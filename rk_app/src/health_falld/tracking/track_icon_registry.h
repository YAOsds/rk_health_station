#pragma once

#include <QHash>
#include <QSet>
#include <QVector>

class TrackIconRegistry {
public:
    explicit TrackIconRegistry(int maxIcons);

    int iconIdForTrack(int trackId);
    void reconcileActiveTracks(const QVector<int> &activeTrackIds);

private:
    int takeNextIconId();

    int maxIcons_ = 0;
    QHash<int, int> trackToIcon_;
    QSet<int> usedIcons_;
};

#include "action/target_selector.h"

#include <algorithm>

PosePerson TargetSelector::selectPrimary(const QVector<PosePerson> &people) const {
    if (people.isEmpty()) {
        return PosePerson();
    }

    const auto best = std::max_element(people.begin(), people.end(),
        [](const PosePerson &left, const PosePerson &right) {
            return left.score < right.score;
        });
    return best == people.end() ? PosePerson() : *best;
}

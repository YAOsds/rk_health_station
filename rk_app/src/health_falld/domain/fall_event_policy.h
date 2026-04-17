#pragma once

#include "models/fall_models.h"

#include <optional>

class FallEventPolicy {
public:
    std::optional<FallEvent> update(const QString &rawState, double confidence);

private:
    int fallLikeCount_ = 0;
};

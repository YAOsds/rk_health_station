#pragma once

#include "action/action_classifier.h"
#include "runtime/runtime_config.h"

#include <memory>

std::unique_ptr<ActionClassifier> createActionClassifier(const FallRuntimeConfig &config);
QString actionModelPathForConfig(const FallRuntimeConfig &config);

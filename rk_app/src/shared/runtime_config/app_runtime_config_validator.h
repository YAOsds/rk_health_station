#pragma once

#include "runtime_config/app_runtime_config.h"

void validateAppRuntimeConfig(
    const AppRuntimeConfig &config, QStringList *errors, QStringList *warnings);

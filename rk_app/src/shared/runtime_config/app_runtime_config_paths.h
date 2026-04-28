#pragma once

#include "runtime_config/app_runtime_config.h"

QString resolveRuntimeConfigPath(const QString &explicitPath);
void normalizeRuntimeConfigPaths(const QString &configPath, AppRuntimeConfig *config);

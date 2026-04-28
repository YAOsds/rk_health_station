#include "runtime_config/app_runtime_config_paths.h"

#include <QDir>
#include <QFileInfo>

namespace {
QString normalizeOptionalPath(const QString &baseDir, const QString &path) {
    if (path.isEmpty()) {
        return path;
    }

    QFileInfo info(path);
    if (info.isAbsolute() || baseDir.isEmpty()) {
        return QDir::cleanPath(path);
    }
    return QDir::cleanPath(QDir(baseDir).absoluteFilePath(path));
}
}

QString resolveRuntimeConfigPath(const QString &explicitPath) {
    QString selected = explicitPath.trimmed();
    if (selected.isEmpty()) {
        selected = qEnvironmentVariable("RK_APP_CONFIG_PATH").trimmed();
    }
    if (selected.isEmpty()) {
        return QString();
    }

    const QFileInfo info(selected);
    if (info.isAbsolute()) {
        return info.absoluteFilePath();
    }
    return QFileInfo(QDir::current(), selected).absoluteFilePath();
}

void normalizeRuntimeConfigPaths(const QString &configPath, AppRuntimeConfig *config) {
    if (!config) {
        return;
    }

    const QString baseDir = configPath.isEmpty() ? QString() : QFileInfo(configPath).absolutePath();
    config->paths.databasePath = normalizeOptionalPath(baseDir, config->paths.databasePath);
    config->ipc.healthSocketPath = normalizeOptionalPath(baseDir, config->ipc.healthSocketPath);
    config->ipc.videoSocketPath = normalizeOptionalPath(baseDir, config->ipc.videoSocketPath);
    config->ipc.analysisSocketPath = normalizeOptionalPath(baseDir, config->ipc.analysisSocketPath);
    config->ipc.fallSocketPath = normalizeOptionalPath(baseDir, config->ipc.fallSocketPath);
    config->fallDetection.poseModelPath = normalizeOptionalPath(baseDir, config->fallDetection.poseModelPath);
    config->fallDetection.stgcnModelPath = normalizeOptionalPath(baseDir, config->fallDetection.stgcnModelPath);
    config->fallDetection.lstmModelPath = normalizeOptionalPath(baseDir, config->fallDetection.lstmModelPath);
    config->fallDetection.lstmWeightsPath = normalizeOptionalPath(baseDir, config->fallDetection.lstmWeightsPath);
    config->fallDetection.actionModelPath = normalizeOptionalPath(baseDir, config->fallDetection.actionModelPath);
    config->debug.healthdEventMarkerPath = normalizeOptionalPath(baseDir, config->debug.healthdEventMarkerPath);
    config->debug.videoLatencyMarkerPath = normalizeOptionalPath(baseDir, config->debug.videoLatencyMarkerPath);
    config->debug.fallLatencyMarkerPath = normalizeOptionalPath(baseDir, config->debug.fallLatencyMarkerPath);
    config->debug.fallPoseTimingPath = normalizeOptionalPath(baseDir, config->debug.fallPoseTimingPath);
    config->debug.fallTrackTracePath = normalizeOptionalPath(baseDir, config->debug.fallTrackTracePath);
    config->debug.fallLstmTracePath = normalizeOptionalPath(baseDir, config->debug.fallLstmTracePath);
}

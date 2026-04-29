#include "app/config_editor_app.h"
#include "runtime_config/app_runtime_config_paths.h"

#include <QApplication>
#include <QDir>

namespace {
QString configPathFromArgs(const QStringList &arguments) {
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("--config") && i + 1 < arguments.size()) {
            return resolveRuntimeConfigPath(arguments.at(i + 1));
        }
    }
    return QString();
}
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QString configPath = configPathFromArgs(app.arguments());
    if (configPath.isEmpty()) {
        configPath = resolveRuntimeConfigPath(QString());
    }
    if (configPath.isEmpty()) {
        configPath = QDir::current().filePath(QStringLiteral("runtime_config.json"));
    }

    ConfigEditorApp editor(configPath);
    if (!editor.start()) {
        return 1;
    }

    return app.exec();
}

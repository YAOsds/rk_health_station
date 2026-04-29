#pragma once

#include <memory>

class ConfigEditorWindow;
class QObject;
class QString;

class ConfigEditorApp {
public:
    explicit ConfigEditorApp(const QString &configPath);
    ~ConfigEditorApp();

    bool start();

private:
    std::unique_ptr<ConfigEditorWindow> window_;
};

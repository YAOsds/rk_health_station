#include "app/config_editor_app.h"

#include "widgets/config_editor_window.h"

ConfigEditorApp::ConfigEditorApp(const QString &configPath)
    : window_(std::make_unique<ConfigEditorWindow>(configPath)) {
}

ConfigEditorApp::~ConfigEditorApp() = default;

bool ConfigEditorApp::start() {
    window_->load();
    window_->show();
    return true;
}

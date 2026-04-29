#pragma once

#include "runtime_config/app_runtime_config.h"

#include <QHash>
#include <QMainWindow>

class QLabel;
class QWidget;

class ConfigEditorWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ConfigEditorWindow(const QString &configPath, QWidget *parent = nullptr);

    bool load();
    QString valueForField(const QString &fieldPath) const;
    QWidget *fieldWidget(const QString &fieldPath) const;

public slots:
    bool save();
    void restoreDefaults();

private:
    void buildUi();
    void populateFromConfig(const AppRuntimeConfig &config);
    AppRuntimeConfig collectConfigFromWidgets() const;
    void setDirty(bool dirty);
    void setStatus(const QString &text, bool error = false);
    void refreshWindowTitle();

    QWidget *registerLineEdit(const QString &fieldPath);
    QWidget *registerCheckBox(const QString &fieldPath);
    QWidget *registerComboBox(const QString &fieldPath,
        const QList<QPair<QString, QString>> &options);
    QWidget *registerSpinBox(const QString &fieldPath, int minimum, int maximum);
    QWidget *registerDoubleSpinBox(const QString &fieldPath, double minimum, double maximum,
        int decimals);

    QString configPath_;
    AppRuntimeConfig currentConfig_;
    QLabel *statusLabel_ = nullptr;
    QHash<QString, QWidget *> fieldWidgets_;
    bool dirty_ = false;
};

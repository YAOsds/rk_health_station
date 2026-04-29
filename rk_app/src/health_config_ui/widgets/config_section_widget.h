#pragma once

#include <QGroupBox>

class QFormLayout;
class QLabel;

class ConfigSectionWidget : public QGroupBox {
    Q_OBJECT

public:
    explicit ConfigSectionWidget(
        const QString &title, const QString &description = QString(), QWidget *parent = nullptr);

    QFormLayout *formLayout() const;

private:
    QLabel *descriptionLabel_ = nullptr;
    QFormLayout *formLayout_ = nullptr;
};

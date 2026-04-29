#include "widgets/config_section_widget.h"

#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

ConfigSectionWidget::ConfigSectionWidget(
    const QString &title, const QString &description, QWidget *parent)
    : QGroupBox(title, parent)
    , descriptionLabel_(new QLabel(description, this))
    , formLayout_(new QFormLayout()) {
    descriptionLabel_->setWordWrap(true);
    descriptionLabel_->setVisible(!description.isEmpty());

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(descriptionLabel_);
    layout->addLayout(formLayout_);
    formLayout_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
}

QFormLayout *ConfigSectionWidget::formLayout() const {
    return formLayout_;
}

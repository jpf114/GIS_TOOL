#include "param_widget.h"
#include "param_card_widget.h"
#include "style_constants.h"

#include <gis/framework/param_spec.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QScrollArea>

#include <array>

ParamWidget::ParamWidget(QWidget* parent)
    : QWidget(parent) {
    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));

    auto* container = new QWidget;
    container->setStyleSheet(QStringLiteral("background: transparent;"));

    mainLayout_ = new QVBoxLayout(container);
    mainLayout_->setContentsMargins(
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding);
    mainLayout_->setSpacing(gis::style::Size::kCardSpacing);
    mainLayout_->addStretch();

    scrollArea->setWidget(container);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

void ParamWidget::setUiContext(const std::string& pluginName, const std::string& actionKey) {
    pluginName_ = pluginName;
    actionKey_ = actionKey;
}

bool ParamWidget::isInputParam(const gis::framework::ParamSpec& spec) const {
    if (spec.key == "action") return false;
    if (spec.key == "output" || spec.key.find("output") != std::string::npos) return false;
    return spec.required;
}

bool ParamWidget::isOutputParam(const gis::framework::ParamSpec& spec) const {
    if (spec.key == "output" || spec.key.find("output") != std::string::npos) return true;
    return false;
}

void ParamWidget::setParamSpecs(const std::vector<gis::framework::ParamSpec>& specs) {
    specs_ = specs;
    buildCards();
}

void ParamWidget::buildCards() {
    if (inputCard_) { mainLayout_->removeWidget(inputCard_); delete inputCard_; inputCard_ = nullptr; }
    if (outputCard_) { mainLayout_->removeWidget(outputCard_); delete outputCard_; outputCard_ = nullptr; }
    if (advancedCard_) { mainLayout_->removeWidget(advancedCard_); delete advancedCard_; advancedCard_ = nullptr; }

    QLayoutItem* stretchItem = nullptr;
    while (mainLayout_->count() > 0) {
        QLayoutItem* item = mainLayout_->takeAt(0);
        if (item->spacerItem()) {
            delete item;
        } else {
            delete item->widget();
            delete item;
        }
    }

    if (specs_.empty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("\345\275\223\345\211\215\345\255\220\345\212\237\350\203\275\346\232\202\346\227\240\345\217\257\351\205\215\347\275\256\345\217\202\346\225\260\343\200\202"));
        emptyLabel->setWordWrap(true);
        emptyLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; padding: 24px;").arg(gis::style::Color::kTextMuted));
        mainLayout_->addWidget(emptyLabel);
        mainLayout_->addStretch();
        return;
    }

    bool hasInput = false, hasOutput = false, hasAdvanced = false;
    for (const auto& spec : specs_) {
        if (spec.key == "action") continue;
        if (isInputParam(spec)) hasInput = true;
        else if (isOutputParam(spec)) hasOutput = true;
        else hasAdvanced = true;
    }

    if (hasInput) {
        inputCard_ = new ParamCardWidget(ParamCardWidget::CardType::Input);
        inputCard_->setUiContext(pluginName_, actionKey_);
        for (const auto& spec : specs_) {
            if (spec.key == "action") continue;
            if (isInputParam(spec)) inputCard_->addParam(spec);
        }
        connect(inputCard_, &ParamCardWidget::paramChanged, this, &ParamWidget::paramsChanged);
        mainLayout_->addWidget(inputCard_);
    }

    if (hasOutput) {
        outputCard_ = new ParamCardWidget(ParamCardWidget::CardType::Output);
        outputCard_->setUiContext(pluginName_, actionKey_);
        for (const auto& spec : specs_) {
            if (isOutputParam(spec)) outputCard_->addParam(spec);
        }
        connect(outputCard_, &ParamCardWidget::paramChanged, this, &ParamWidget::paramsChanged);
        mainLayout_->addWidget(outputCard_);
    }

    if (hasAdvanced) {
        advancedCard_ = new ParamCardWidget(ParamCardWidget::CardType::Advanced);
        advancedCard_->setUiContext(pluginName_, actionKey_);
        for (const auto& spec : specs_) {
            if (spec.key == "action") continue;
            if (!isInputParam(spec) && !isOutputParam(spec)) advancedCard_->addParam(spec);
        }
        connect(advancedCard_, &ParamCardWidget::paramChanged, this, &ParamWidget::paramsChanged);
        mainLayout_->addWidget(advancedCard_);
    }

    mainLayout_->addStretch();
}

std::map<std::string, gis::framework::ParamValue> ParamWidget::collectParams() const {
    std::map<std::string, gis::framework::ParamValue> params;

    auto mergeCard = [&](const ParamCardWidget* card) {
        if (!card) return;
        auto cardValues = card->collectValues();
        for (auto it = cardValues.constBegin(); it != cardValues.constEnd(); ++it) {
            params[it.key()] = it.value();
        }
    };

    mergeCard(inputCard_);
    mergeCard(outputCard_);
    mergeCard(advancedCard_);

    return params;
}

void ParamWidget::clear() {
    specs_.clear();
    if (inputCard_) { delete inputCard_; inputCard_ = nullptr; }
    if (outputCard_) { delete outputCard_; outputCard_ = nullptr; }
    if (advancedCard_) { delete advancedCard_; advancedCard_ = nullptr; }

    QLayoutItem* item = nullptr;
    while ((item = mainLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    mainLayout_->addStretch();
}

bool ParamWidget::hasParam(const std::string& key) const {
    for (const auto& spec : specs_) {
        if (spec.key == key) return true;
    }
    return false;
}

void ParamWidget::setStringValue(const std::string& key, const std::string& value) {
    auto applyToCard = [&](ParamCardWidget* card) -> bool {
        if (!card || !card->hasParam(key)) return false;
        card->setStringValue(key, value);
        return true;
    };

    if (applyToCard(inputCard_)) return;
    if (applyToCard(outputCard_)) return;
    if (applyToCard(advancedCard_)) return;
}

bool ParamWidget::setValueFromString(const std::string& key, const std::string& value) {
    auto applyToCard = [&](ParamCardWidget* card) -> bool {
        if (!card || !card->hasParam(key)) {
            return false;
        }
        return card->setValueFromString(key, value);
    };

    if (applyToCard(inputCard_)) return true;
    if (applyToCard(outputCard_)) return true;
    if (applyToCard(advancedCard_)) return true;
    return false;
}

void ParamWidget::setExtentValue(const std::string& key, const std::array<double, 4>& value) {
    auto applyToCard = [&](ParamCardWidget* card) -> bool {
        if (!card || !card->hasParam(key)) return false;
        card->setExtentValue(key, value);
        return true;
    };

    if (applyToCard(inputCard_)) return;
    if (applyToCard(outputCard_)) return;
    if (applyToCard(advancedCard_)) return;
}

std::string ParamWidget::stringValue(const std::string& key) const {
    auto params = collectParams();
    auto it = params.find(key);
    if (it != params.end()) {
        if (auto* str = std::get_if<std::string>(&it->second)) {
            return *str;
        }
    }
    return {};
}

void ParamWidget::setHighlightedParam(const std::string& key) {
    if (inputCard_) inputCard_->markFieldError(key, !key.empty());
    if (outputCard_) outputCard_->markFieldError(key, !key.empty());
    if (advancedCard_) advancedCard_->markFieldError(key, !key.empty());
}

bool ParamWidget::validate() const {
    bool valid = true;
    if (inputCard_ && !inputCard_->validate()) valid = false;
    if (outputCard_ && !outputCard_->validate()) valid = false;
    if (advancedCard_ && !advancedCard_->validate()) valid = false;
    return valid;
}

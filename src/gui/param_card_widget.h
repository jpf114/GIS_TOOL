#pragma once

#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QMap>
#include <memory>
#include <string>

#include <gis/framework/param_spec.h>
#include "gui_data_support.h"

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPushButton;

struct ParamWidgetEntry {
    std::string key;
    QWidget* widget = nullptr;
    QLineEdit* lineEdit = nullptr;
    QComboBox* comboBox = nullptr;
    QDoubleSpinBox* spinBox = nullptr;
    QSpinBox* intSpinBox = nullptr;
    QCheckBox* checkBox = nullptr;
    QPushButton* browseButton = nullptr;
    QPushButton* auxButton = nullptr;
    QPushButton* secondaryAuxButton = nullptr;
};

class ParamCardWidget : public QWidget {
    Q_OBJECT
public:
    enum class CardType { Input, Output, Advanced };

    explicit ParamCardWidget(CardType type, QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setUiContext(const std::string& pluginName, const std::string& actionKey);
    void addParam(const gis::framework::ParamSpec& spec);
    void clearParams();

    QMap<std::string, gis::framework::ParamValue> collectValues() const;
    bool validate() const;
    bool hasParam(const std::string& key) const;
    void setStringValue(const std::string& key, const std::string& value);
    bool setValueFromString(const std::string& key, const std::string& value);
    void setExtentValue(const std::string& key, const std::array<double, 4>& value);

    void markFieldError(const std::string& key, bool error) const;

signals:
    void paramChanged();

private:
    void setupUi();
    QWidget* createParamWidget(const gis::framework::ParamSpec& spec, ParamWidgetEntry& entry);
    QWidget* createFileWidget(const gis::framework::ParamSpec& spec, ParamWidgetEntry& entry);
    QWidget* createEnumWidget(const gis::framework::ParamSpec& spec, ParamWidgetEntry& entry);
    QWidget* createIntWidget(const gis::framework::ParamSpec& spec, ParamWidgetEntry& entry);
    QWidget* createNumberWidget(const gis::framework::ParamSpec& spec, ParamWidgetEntry& entry);
    QWidget* createBoolWidget(const gis::framework::ParamSpec& spec, ParamWidgetEntry& entry);
    QWidget* createTextWidget(const gis::framework::ParamSpec& spec, ParamWidgetEntry& entry);
    QWidget* createExtentWidget(const gis::framework::ParamSpec& spec, ParamWidgetEntry& entry);

    CardType cardType_;
    QFrame* cardFrame_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* iconLabel_ = nullptr;
    QGridLayout* paramsLayout_ = nullptr;
    QVBoxLayout* cardContentLayout_ = nullptr;
    int paramsRow_ = 0;
    std::string pluginName_;
    std::string actionKey_;

    QMap<std::string, ParamWidgetEntry> entries_;
    QMap<std::string, bool> requiredMap_;
};

#pragma once

#include <QWidget>
#include <gis/framework/param_spec.h>
#include <array>
#include <map>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

class ParamWidget : public QWidget {
    Q_OBJECT
public:
    explicit ParamWidget(QWidget* parent = nullptr);

    void setParamSpecs(const std::vector<gis::framework::ParamSpec>& specs);
    std::map<std::string, gis::framework::ParamValue> collectParams() const;
    void clear();
    bool hasParam(const std::string& key) const;
    void setStringValue(const std::string& key, const std::string& value);
    void setExtentValue(const std::string& key, const std::array<double, 4>& value);
    std::string stringValue(const std::string& key) const;

private:
    struct WidgetRow {
        gis::framework::ParamSpec spec;
        QWidget* editor = nullptr;
        QWidget* browseBtn = nullptr;
    };

    void buildForm();
    QWidget* createEditor(const gis::framework::ParamSpec& spec);
    gis::framework::ParamValue collectValue(const WidgetRow& row) const;

    std::vector<WidgetRow> rows_;
    std::vector<gis::framework::ParamSpec> specs_;
};

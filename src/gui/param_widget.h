#pragma once
#include <QWidget>
#include <gis/framework/param_spec.h>
#include <map>
#include <vector>

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;

class ParamWidget : public QWidget {
    Q_OBJECT
public:
    explicit ParamWidget(QWidget* parent = nullptr);

    void setParamSpecs(const std::vector<gis::framework::ParamSpec>& specs);
    std::map<std::string, gis::framework::ParamValue> collectParams() const;
    void clear();

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

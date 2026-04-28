#pragma once

#include <QWidget>
#include <gis/framework/param_spec.h>
#include <array>
#include <map>
#include <vector>
#include <string>

class QVBoxLayout;
class ParamCardWidget;

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
    void setHighlightedParam(const std::string& key);
    bool validate() const;

signals:
    void paramsChanged();

private:
    void buildCards();
    bool isInputParam(const gis::framework::ParamSpec& spec) const;
    bool isOutputParam(const gis::framework::ParamSpec& spec) const;

    QVBoxLayout* mainLayout_ = nullptr;
    ParamCardWidget* inputCard_ = nullptr;
    ParamCardWidget* outputCard_ = nullptr;
    ParamCardWidget* advancedCard_ = nullptr;

    std::vector<gis::framework::ParamSpec> specs_;
};

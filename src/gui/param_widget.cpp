#include "param_widget.h"

#include "crs_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

#include <array>

ParamWidget::ParamWidget(QWidget* parent)
    : QWidget(parent) {}

void ParamWidget::setParamSpecs(const std::vector<gis::framework::ParamSpec>& specs) {
    specs_ = specs;
    buildForm();
}

std::map<std::string, gis::framework::ParamValue> ParamWidget::collectParams() const {
    std::map<std::string, gis::framework::ParamValue> params;
    for (const auto& row : rows_) {
        params[row.spec.key] = collectValue(row);
    }
    return params;
}

void ParamWidget::clear() {
    rows_.clear();
    specs_.clear();
    if (QLayout* layout = this->layout()) {
        QLayoutItem* item = nullptr;
        while ((item = layout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete layout;
    }
}

bool ParamWidget::hasParam(const std::string& key) const {
    for (const auto& row : rows_) {
        if (row.spec.key == key) {
            return true;
        }
    }
    return false;
}

void ParamWidget::setStringValue(const std::string& key, const std::string& value) {
    for (auto& row : rows_) {
        if (row.spec.key != key) {
            continue;
        }

        switch (row.spec.type) {
            case gis::framework::ParamType::String:
            case gis::framework::ParamType::FilePath:
            case gis::framework::ParamType::DirPath:
            case gis::framework::ParamType::CRS: {
                if (auto* edit = qobject_cast<QLineEdit*>(row.editor)) {
                    edit->setText(QString::fromUtf8(value));
                }
                return;
            }
            case gis::framework::ParamType::Enum: {
                if (auto* combo = qobject_cast<QComboBox*>(row.editor)) {
                    const int index = combo->findText(QString::fromUtf8(value));
                    if (index >= 0) {
                        combo->setCurrentIndex(index);
                    }
                }
                return;
            }
            default:
                return;
        }
    }
}

std::string ParamWidget::stringValue(const std::string& key) const {
    for (const auto& row : rows_) {
        if (row.spec.key != key) {
            continue;
        }

        switch (row.spec.type) {
            case gis::framework::ParamType::String:
            case gis::framework::ParamType::FilePath:
            case gis::framework::ParamType::DirPath:
            case gis::framework::ParamType::CRS: {
                if (auto* edit = qobject_cast<QLineEdit*>(row.editor)) {
                    return edit->text().toUtf8().constData();
                }
                return {};
            }
            case gis::framework::ParamType::Enum: {
                if (auto* combo = qobject_cast<QComboBox*>(row.editor)) {
                    return combo->currentText().toUtf8().constData();
                }
                return {};
            }
            default:
                return {};
        }
    }
    return {};
}

void ParamWidget::buildForm() {
    clear();

    auto* formLayout = new QFormLayout;
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    for (const auto& spec : specs_) {
        WidgetRow row;
        row.spec = spec;

        QWidget* editor = createEditor(spec);
        row.editor = editor;

        QString label = QString::fromUtf8(spec.displayName);
        if (spec.required) {
            label += QStringLiteral(" *");
        }

        if (spec.type == gis::framework::ParamType::FilePath ||
            spec.type == gis::framework::ParamType::DirPath ||
            spec.type == gis::framework::ParamType::CRS) {
            auto* container = new QWidget;
            auto* hLayout = new QHBoxLayout(container);
            hLayout->setContentsMargins(0, 0, 0, 0);
            hLayout->addWidget(editor);

            auto* browseBtn = new QPushButton(QStringLiteral("..."));
            browseBtn->setFixedWidth(40);
            hLayout->addWidget(browseBtn);
            row.browseBtn = browseBtn;

            if (spec.type == gis::framework::ParamType::FilePath) {
                connect(browseBtn, &QPushButton::clicked, this, [this, editor]() {
                    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择文件"));
                    if (!path.isEmpty()) {
                        qobject_cast<QLineEdit*>(editor)->setText(path);
                    }
                });
            } else if (spec.type == gis::framework::ParamType::DirPath) {
                connect(browseBtn, &QPushButton::clicked, this, [this, editor]() {
                    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择目录"));
                    if (!path.isEmpty()) {
                        qobject_cast<QLineEdit*>(editor)->setText(path);
                    }
                });
            } else if (spec.type == gis::framework::ParamType::CRS) {
                connect(browseBtn, &QPushButton::clicked, this, [this, editor]() {
                    CrsDialog dlg(this);
                    if (dlg.exec() == QDialog::Accepted && !dlg.selectedCrs().isEmpty()) {
                        qobject_cast<QLineEdit*>(editor)->setText(dlg.selectedCrs());
                    }
                });
            }

            formLayout->addRow(label, container);
        } else if (spec.type == gis::framework::ParamType::Extent) {
            delete editor;
            editor = nullptr;

            auto* extentWidget = new QWidget;
            auto* gridLayout = new QHBoxLayout(extentWidget);
            gridLayout->setContentsMargins(0, 0, 0, 0);

            auto* xminSpin = new QDoubleSpinBox;
            xminSpin->setRange(-1e15, 1e15);
            xminSpin->setDecimals(6);
            xminSpin->setPrefix("Xmin: ");
            gridLayout->addWidget(xminSpin);

            auto* yminSpin = new QDoubleSpinBox;
            yminSpin->setRange(-1e15, 1e15);
            yminSpin->setDecimals(6);
            yminSpin->setPrefix("Ymin: ");
            gridLayout->addWidget(yminSpin);

            auto* xmaxSpin = new QDoubleSpinBox;
            xmaxSpin->setRange(-1e15, 1e15);
            xmaxSpin->setDecimals(6);
            xmaxSpin->setPrefix("Xmax: ");
            gridLayout->addWidget(xmaxSpin);

            auto* ymaxSpin = new QDoubleSpinBox;
            ymaxSpin->setRange(-1e15, 1e15);
            ymaxSpin->setDecimals(6);
            ymaxSpin->setPrefix("Ymax: ");
            gridLayout->addWidget(ymaxSpin);

            if (auto* defaultExt = std::get_if<std::array<double, 4>>(&spec.defaultValue)) {
                xminSpin->setValue((*defaultExt)[0]);
                yminSpin->setValue((*defaultExt)[1]);
                xmaxSpin->setValue((*defaultExt)[2]);
                ymaxSpin->setValue((*defaultExt)[3]);
            }

            row.editor = extentWidget;
            formLayout->addRow(label, extentWidget);
        } else {
            formLayout->addRow(label, editor);
        }

        if (!spec.description.empty()) {
            auto* descLabel = new QLabel(QString::fromUtf8(spec.description));
            descLabel->setWordWrap(true);
            descLabel->setStyleSheet("color: gray; font-size: small;");
            formLayout->addRow(QString(), descLabel);
        }

        rows_.push_back(row);
    }

    setLayout(formLayout);
}

QWidget* ParamWidget::createEditor(const gis::framework::ParamSpec& spec) {
    switch (spec.type) {
        case gis::framework::ParamType::String:
        case gis::framework::ParamType::FilePath:
        case gis::framework::ParamType::DirPath:
        case gis::framework::ParamType::CRS: {
            auto* edit = new QLineEdit;
            if (auto* defStr = std::get_if<std::string>(&spec.defaultValue); defStr && !defStr->empty()) {
                edit->setText(QString::fromUtf8(*defStr));
            }
            return edit;
        }
        case gis::framework::ParamType::Int: {
            auto* spin = new QSpinBox;
            spin->setRange(-2147483647, 2147483647);
            if (auto* defInt = std::get_if<int>(&spec.defaultValue)) {
                spin->setValue(*defInt);
            }
            auto* minInt = std::get_if<int>(&spec.minValue);
            auto* maxInt = std::get_if<int>(&spec.maxValue);
            if (minInt && maxInt && *maxInt > *minInt) {
                spin->setRange(*minInt, *maxInt);
            }
            return spin;
        }
        case gis::framework::ParamType::Double: {
            auto* spin = new QDoubleSpinBox;
            spin->setRange(-1e15, 1e15);
            spin->setDecimals(6);
            if (auto* defDouble = std::get_if<double>(&spec.defaultValue)) {
                spin->setValue(*defDouble);
            }
            return spin;
        }
        case gis::framework::ParamType::Bool: {
            auto* check = new QCheckBox(QStringLiteral("启用"));
            if (auto* defBool = std::get_if<bool>(&spec.defaultValue)) {
                check->setChecked(*defBool);
            }
            return check;
        }
        case gis::framework::ParamType::Enum: {
            auto* combo = new QComboBox;
            for (const auto& val : spec.enumValues) {
                combo->addItem(QString::fromUtf8(val));
            }
            if (auto* defStr = std::get_if<std::string>(&spec.defaultValue)) {
                const int index = combo->findText(QString::fromUtf8(*defStr));
                if (index >= 0) {
                    combo->setCurrentIndex(index);
                }
            }
            return combo;
        }
        case gis::framework::ParamType::Extent:
            return new QWidget;
    }

    return new QWidget;
}

gis::framework::ParamValue ParamWidget::collectValue(const WidgetRow& row) const {
    switch (row.spec.type) {
        case gis::framework::ParamType::String:
        case gis::framework::ParamType::FilePath:
        case gis::framework::ParamType::DirPath:
        case gis::framework::ParamType::CRS: {
            auto* edit = qobject_cast<QLineEdit*>(row.editor);
            return edit ? edit->text().toUtf8().constData() : std::string{};
        }
        case gis::framework::ParamType::Int: {
            auto* spin = qobject_cast<QSpinBox*>(row.editor);
            return spin ? spin->value() : int{0};
        }
        case gis::framework::ParamType::Double: {
            auto* spin = qobject_cast<QDoubleSpinBox*>(row.editor);
            return spin ? spin->value() : double{0};
        }
        case gis::framework::ParamType::Bool: {
            auto* check = qobject_cast<QCheckBox*>(row.editor);
            return check ? check->isChecked() : false;
        }
        case gis::framework::ParamType::Enum: {
            auto* combo = qobject_cast<QComboBox*>(row.editor);
            return combo ? combo->currentText().toUtf8().constData() : std::string{};
        }
        case gis::framework::ParamType::Extent: {
            auto* extentWidget = row.editor;
            const auto spins = extentWidget->findChildren<QDoubleSpinBox*>();
            std::array<double, 4> arr{0, 0, 0, 0};
            if (spins.size() >= 4) {
                arr[0] = spins[0]->value();
                arr[1] = spins[1]->value();
                arr[2] = spins[2]->value();
                arr[3] = spins[3]->value();
            }
            return arr;
        }
    }

    return std::string{};
}

#include "param_widget.h"
#include "crs_dialog.h"
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QPushButton>
#include <array>

ParamWidget::ParamWidget(QWidget* parent)
    : QWidget(parent) {}

void ParamWidget::setParamSpecs(const std::vector<gis::framework::ParamSpec>& specs) {
    specs_ = specs;
    buildForm();
}

std::map<std::string, gis::framework::ParamValue> ParamWidget::collectParams() const {
    std::map<std::string, gis::framework::ParamValue> params;
    for (auto& row : rows_) {
        params[row.spec.key] = collectValue(row);
    }
    return params;
}

void ParamWidget::clear() {
    rows_.clear();
    specs_.clear();
    QLayout* layout = this->layout();
    if (layout) {
        QLayoutItem* item;
        while ((item = layout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete layout;
    }
}

void ParamWidget::buildForm() {
    clear();

    auto* formLayout = new QFormLayout;
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    for (auto& spec : specs_) {
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
                    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择文件"));
                    if (!path.isEmpty()) {
                        qobject_cast<QLineEdit*>(editor)->setText(path);
                    }
                });
            } else if (spec.type == gis::framework::ParamType::DirPath) {
                connect(browseBtn, &QPushButton::clicked, this, [this, editor]() {
                    QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择目录"));
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

            auto* defaultExt = std::get_if<std::array<double, 4>>(&spec.defaultValue);
            if (defaultExt) {
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
            auto* defStr = std::get_if<std::string>(&spec.defaultValue);
            if (defStr && !defStr->empty()) {
                edit->setText(QString::fromUtf8(*defStr));
            }
            return edit;
        }
        case gis::framework::ParamType::Int: {
            auto* spin = new QSpinBox;
            spin->setRange(-2147483647, 2147483647);
            auto* defInt = std::get_if<int>(&spec.defaultValue);
            if (defInt) spin->setValue(*defInt);
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
            auto* defDouble = std::get_if<double>(&spec.defaultValue);
            if (defDouble) spin->setValue(*defDouble);
            return spin;
        }
        case gis::framework::ParamType::Bool: {
            auto* check = new QCheckBox(QStringLiteral("启用"));
            auto* defBool = std::get_if<bool>(&spec.defaultValue);
            if (defBool) check->setChecked(*defBool);
            return check;
        }
        case gis::framework::ParamType::Enum: {
            auto* combo = new QComboBox;
            for (auto& val : spec.enumValues) {
                combo->addItem(QString::fromUtf8(val));
            }
            auto* defStr = std::get_if<std::string>(&spec.defaultValue);
            if (defStr) {
                int idx = combo->findText(QString::fromUtf8(*defStr));
                if (idx >= 0) combo->setCurrentIndex(idx);
            }
            return combo;
        }
        case gis::framework::ParamType::Extent: {
            return new QWidget;
        }
    }
    return new QWidget;
}

gis::framework::ParamValue ParamWidget::collectValue(const WidgetRow& row) const {
    const auto& spec = row.spec;

    switch (spec.type) {
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
            auto spins = extentWidget->findChildren<QDoubleSpinBox*>();
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

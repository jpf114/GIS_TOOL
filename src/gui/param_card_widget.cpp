#include "param_card_widget.h"
#include "style_constants.h"

#include <gis/framework/param_spec.h>
#include <gis/framework/result.h>

#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>

ParamCardWidget::ParamCardWidget(CardType type, QWidget* parent)
    : QWidget(parent), cardType_(type) {
    setupUi();
}

void ParamCardWidget::setupUi() {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    cardFrame_ = new QFrame;
    cardFrame_->setObjectName(QStringLiteral("card"));

    cardContentLayout_ = new QVBoxLayout(cardFrame_);
    cardContentLayout_->setContentsMargins(
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding);
    cardContentLayout_->setSpacing(12);

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(8);

    static const QMap<CardType, QString> kIcons = {
        {CardType::Input,    QStringLiteral("\360\237\223\245")},
        {CardType::Output,   QStringLiteral("\360\237\223\244")},
        {CardType::Advanced, QStringLiteral("\342\232\231")},
    };
    static const QMap<CardType, QString> kDefaultTitles = {
        {CardType::Input,    QStringLiteral("\350\276\223\345\205\245\345\217\202\346\225\260")},
        {CardType::Output,   QStringLiteral("\350\276\223\345\207\272\345\217\202\346\225\260")},
        {CardType::Advanced, QStringLiteral("\351\253\230\347\272\247\345\217\202\346\225\260")},
    };

    iconLabel_ = new QLabel(kIcons.value(cardType_));
    iconLabel_->setFixedSize(20, 20);
    headerLayout->addWidget(iconLabel_);

    titleLabel_ = new QLabel(kDefaultTitles.value(cardType_));
    titleLabel_->setObjectName(QStringLiteral("cardTitle"));
    headerLayout->addWidget(titleLabel_);
    headerLayout->addStretch();

    cardContentLayout_->addLayout(headerLayout);

    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("background: %1; max-height: 1px;").arg(gis::style::Color::kDivider));
    cardContentLayout_->addWidget(separator);

    auto* paramsContainer = new QWidget;
    paramsLayout_ = new QGridLayout(paramsContainer);
    paramsLayout_->setContentsMargins(0, 0, 0, 0);
    paramsLayout_->setHorizontalSpacing(12);
    paramsLayout_->setVerticalSpacing(10);
    paramsLayout_->setColumnStretch(0, gis::style::Size::kLabelInputRatio);
    paramsLayout_->setColumnStretch(1, 5);
    cardContentLayout_->addWidget(paramsContainer);

    outerLayout->addWidget(cardFrame_);
}

void ParamCardWidget::setTitle(const QString& title) {
    titleLabel_->setText(title);
}

void ParamCardWidget::addParam(const gis::framework::ParamSpec& spec) {
    ParamWidgetEntry entry;
    entry.key = spec.key;

    auto* labelWidget = new QWidget;
    auto* labelLayout = new QVBoxLayout(labelWidget);
    labelLayout->setContentsMargins(0, 0, 0, 0);
    labelLayout->setSpacing(2);

    auto* nameLabel = new QLabel(QString::fromUtf8(spec.displayName));
    nameLabel->setObjectName(QStringLiteral("paramLabel"));
    nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    labelLayout->addWidget(nameLabel);

    if (!spec.description.empty()) {
        auto* descLabel = new QLabel(QString::fromUtf8(spec.description));
        descLabel->setObjectName(QStringLiteral("paramDesc"));
        descLabel->setWordWrap(true);
        labelLayout->addWidget(descLabel);
    }

    if (spec.required) {
        auto* reqLabel = new QLabel(QStringLiteral("*"));
        reqLabel->setObjectName(QStringLiteral("requiredMark"));
        reqLabel->setAlignment(Qt::AlignRight);
        labelLayout->addWidget(reqLabel);
    }

    QWidget* inputWidget = createParamWidget(spec, entry);

    paramsLayout_->addWidget(labelWidget, paramsRow_, 0, Qt::AlignRight | Qt::AlignVCenter);
    paramsLayout_->addWidget(inputWidget, paramsRow_, 1);
    paramsRow_++;

    entries_[spec.key] = entry;
    requiredMap_[spec.key] = spec.required;
}

QWidget* ParamCardWidget::createParamWidget(const gis::framework::ParamSpec& spec,
                                             ParamWidgetEntry& entry) {
    if (spec.type == gis::framework::ParamType::Enum && !spec.enumValues.empty()) {
        return createEnumWidget(spec, entry);
    }
    if (spec.type == gis::framework::ParamType::Bool) {
        return createBoolWidget(spec, entry);
    }
    if (spec.type == gis::framework::ParamType::Extent) {
        return createExtentWidget(spec, entry);
    }
    if (spec.type == gis::framework::ParamType::Int) {
        return createIntWidget(spec, entry);
    }
    if (spec.type == gis::framework::ParamType::Double) {
        return createNumberWidget(spec, entry);
    }
    if (spec.type == gis::framework::ParamType::FilePath ||
        spec.type == gis::framework::ParamType::DirPath ||
        spec.type == gis::framework::ParamType::CRS) {
        return createFileWidget(spec, entry);
    }
    return createTextWidget(spec, entry);
}

QWidget* ParamCardWidget::createFileWidget(const gis::framework::ParamSpec& spec,
                                            ParamWidgetEntry& entry) {
    auto* container = new QWidget;
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* lineEdit = new QLineEdit;
    lineEdit->setPlaceholderText(
        spec.type == gis::framework::ParamType::CRS
            ? QStringLiteral("\350\257\267\350\276\223\345\205\245 EPSG \344\273\243\347\240\201...")
            : QStringLiteral("\350\257\267\351\200\211\346\213\251\346\226\207\344\273\266..."));
    if (auto* defStr = std::get_if<std::string>(&spec.defaultValue); defStr && !defStr->empty()) {
        lineEdit->setText(QString::fromUtf8(*defStr));
    }
    entry.lineEdit = lineEdit;
    layout->addWidget(lineEdit, 1);

    if (spec.type != gis::framework::ParamType::CRS) {
        auto* browseBtn = new QPushButton(
            spec.type == gis::framework::ParamType::DirPath
                ? QStringLiteral("\346\265\217\232\232\210...")
                : QStringLiteral("\346\265\217\232\232\210..."));
        browseBtn->setObjectName(QStringLiteral("browseButton"));
        entry.browseButton = browseBtn;

        bool isOutput = (spec.key == "output" || spec.key.find("output") != std::string::npos);
        bool isDir = (spec.type == gis::framework::ParamType::DirPath);
        connect(browseBtn, &QPushButton::clicked, this, [lineEdit, isOutput, isDir]() {
            QString filePath;
            if (isDir) {
                filePath = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("\351\200\211\346\213\251\347\233\256\345\275\225"));
            } else if (isOutput) {
                filePath = QFileDialog::getSaveFileName(nullptr, QStringLiteral("\344\277\235\345\255\230\346\226\207\344\273\266"), QString(),
                    QStringLiteral("\346\211\200\346\234\211\346\226\207\344\273\266 (*);;GeoTIFF (*.tif *.tiff);;Shapefile (*.shp)"));
            } else {
                filePath = QFileDialog::getOpenFileName(nullptr, QStringLiteral("\351\200\211\346\213\251\346\226\207\344\273\266"), QString(),
                    QStringLiteral("\346\211\200\346\234\211\346\226\207\344\273\266 (*);;GeoTIFF (*.tif *.tiff);;Shapefile (*.shp);;JPEG (*.jpg *.jpeg);;PNG (*.png)"));
            }
            if (!filePath.isEmpty()) {
                lineEdit->setText(filePath);
            }
        });
        layout->addWidget(browseBtn);
    }

    return container;
}

QWidget* ParamCardWidget::createEnumWidget(const gis::framework::ParamSpec& spec,
                                            ParamWidgetEntry& entry) {
    auto* comboBox = new QComboBox;
    for (const auto& val : spec.enumValues) {
        comboBox->addItem(QString::fromUtf8(val));
    }
    if (auto* defStr = std::get_if<std::string>(&spec.defaultValue); defStr && !defStr->empty()) {
        int idx = comboBox->findText(QString::fromUtf8(*defStr));
        if (idx >= 0) comboBox->setCurrentIndex(idx);
    }
    entry.comboBox = comboBox;
    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamCardWidget::paramChanged);
    return comboBox;
}

QWidget* ParamCardWidget::createIntWidget(const gis::framework::ParamSpec& spec,
                                           ParamWidgetEntry& entry) {
    auto* spinBox = new QSpinBox;
    spinBox->setRange(-2147483647, 2147483647);
    if (auto* defInt = std::get_if<int>(&spec.defaultValue)) {
        spinBox->setValue(*defInt);
    }
    auto* minInt = std::get_if<int>(&spec.minValue);
    auto* maxInt = std::get_if<int>(&spec.maxValue);
    if (minInt && maxInt && *maxInt > *minInt) {
        spinBox->setRange(*minInt, *maxInt);
    }
    entry.spinBox = new QDoubleSpinBox;
    entry.spinBox->hide();
    entry.widget = spinBox;
    connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { emit paramChanged(); });
    return spinBox;
}

QWidget* ParamCardWidget::createNumberWidget(const gis::framework::ParamSpec& spec,
                                              ParamWidgetEntry& entry) {
    auto* spinBox = new QDoubleSpinBox;
    spinBox->setRange(-1e15, 1e15);
    spinBox->setDecimals(6);
    spinBox->setSingleStep(0.1);
    if (auto* defDouble = std::get_if<double>(&spec.defaultValue)) {
        spinBox->setValue(*defDouble);
    }
    entry.spinBox = spinBox;
    connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ParamCardWidget::paramChanged);
    return spinBox;
}

QWidget* ParamCardWidget::createBoolWidget(const gis::framework::ParamSpec& spec,
                                            ParamWidgetEntry& entry) {
    auto* checkBox = new QCheckBox(QString::fromUtf8(spec.displayName));
    if (auto* defBool = std::get_if<bool>(&spec.defaultValue)) {
        checkBox->setChecked(*defBool);
    }
    entry.checkBox = checkBox;
    connect(checkBox, &QCheckBox::toggled, this, &ParamCardWidget::paramChanged);
    return checkBox;
}

QWidget* ParamCardWidget::createTextWidget(const gis::framework::ParamSpec& spec,
                                            ParamWidgetEntry& entry) {
    auto* lineEdit = new QLineEdit;
    lineEdit->setPlaceholderText(
        !spec.description.empty()
            ? QString::fromUtf8(spec.description)
            : QStringLiteral("\350\257\267\350\276\223\345\205\245..."));
    if (auto* defStr = std::get_if<std::string>(&spec.defaultValue); defStr && !defStr->empty()) {
        lineEdit->setText(QString::fromUtf8(*defStr));
    }
    entry.lineEdit = lineEdit;
    connect(lineEdit, &QLineEdit::textChanged, this, &ParamCardWidget::paramChanged);
    return lineEdit;
}

QWidget* ParamCardWidget::createExtentWidget(const gis::framework::ParamSpec& spec,
                                              ParamWidgetEntry& entry) {
    auto* container = new QWidget;
    auto* grid = new QGridLayout(container);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);

    static const char* kExtentLabels[] = {"Xmin", "Ymin", "Xmax", "Ymax"};

    std::array<double, 4> defaultExtent = {0, 0, 0, 0};
    if (auto* defExt = std::get_if<std::array<double, 4>>(&spec.defaultValue)) {
        defaultExtent = *defExt;
    }

    for (int i = 0; i < 4; ++i) {
        auto* label = new QLabel(QString::fromUtf8(kExtentLabels[i]));
        label->setObjectName(QStringLiteral("paramLabel"));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(label, i / 2, (i % 2) * 2);

        auto* spin = new QDoubleSpinBox;
        spin->setRange(-1e15, 1e15);
        spin->setDecimals(6);
        spin->setPrefix(QString::fromUtf8(kExtentLabels[i]) + QStringLiteral(" "));
        spin->setValue(defaultExtent[i]);
        spin->setMinimumHeight(gis::style::Size::kInputMinHeight);
        grid->addWidget(spin, i / 2, (i % 2) * 2 + 1);
    }

    entry.widget = container;
    return container;
}

void ParamCardWidget::clearParams() {
    while (paramsLayout_->count() > 0) {
        QLayoutItem* item = paramsLayout_->takeAt(0);
        delete item->widget();
        delete item;
    }
    entries_.clear();
    requiredMap_.clear();
    paramsRow_ = 0;
}

QMap<std::string, gis::framework::ParamValue> ParamCardWidget::collectValues() const {
    QMap<std::string, gis::framework::ParamValue> result;

    for (auto it = entries_.constBegin(); it != entries_.constEnd(); ++it) {
        const auto& entry = it.value();
        const std::string& key = entry.key;

        if (entry.lineEdit) {
            result[key] = gis::framework::ParamValue(entry.lineEdit->text().toUtf8().constData());
        } else if (entry.comboBox) {
            result[key] = gis::framework::ParamValue(entry.comboBox->currentText().toUtf8().constData());
        } else if (entry.spinBox && entry.spinBox->isVisible()) {
            if (entry.spinBox->decimals() == 0) {
                result[key] = gis::framework::ParamValue(static_cast<int>(entry.spinBox->value()));
            } else {
                result[key] = gis::framework::ParamValue(entry.spinBox->value());
            }
        } else if (entry.checkBox) {
            result[key] = gis::framework::ParamValue(entry.checkBox->isChecked());
        } else if (entry.widget) {
            auto* spinBox = qobject_cast<QSpinBox*>(entry.widget);
            if (spinBox) {
                result[key] = gis::framework::ParamValue(spinBox->value());
            } else {
                auto spins = entry.widget->findChildren<QDoubleSpinBox*>();
                if (spins.size() >= 4) {
                    std::array<double, 4> arr = {
                        spins[0]->value(), spins[1]->value(),
                        spins[2]->value(), spins[3]->value()
                    };
                    result[key] = gis::framework::ParamValue(arr);
                }
            }
        }
    }

    return result;
}

bool ParamCardWidget::validate() const {
    bool valid = true;
    for (auto it = requiredMap_.constBegin(); it != requiredMap_.constEnd(); ++it) {
        if (!it.value()) continue;

        auto entryIt = entries_.find(it.key());
        if (entryIt == entries_.constEnd()) continue;

        const auto& entry = entryIt.value();
        bool empty = false;

        if (entry.lineEdit) {
            empty = entry.lineEdit->text().trimmed().isEmpty();
        } else if (entry.comboBox) {
            empty = entry.comboBox->currentText().trimmed().isEmpty();
        }

        if (empty) {
            markFieldError(it.key(), true);
            valid = false;
        } else {
            markFieldError(it.key(), false);
        }
    }
    return valid;
}

void ParamCardWidget::markFieldError(const std::string& key, bool error) const {
    auto it = entries_.find(key);
    if (it == entries_.end()) return;

    const auto& entry = it.value();
    QString errorStyle = QString(
        "border: 1px solid %1; background: %2;"
    ).arg(gis::style::Color::kError, gis::style::Color::kErrorBg);
    QString normalStyle;

    if (entry.lineEdit) {
        entry.lineEdit->setStyleSheet(error ? errorStyle : normalStyle);
    } else if (entry.comboBox) {
        entry.comboBox->setStyleSheet(error ? errorStyle : normalStyle);
    }
}

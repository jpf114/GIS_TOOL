#include "param_card_widget.h"
#include "style_constants.h"

#include <gis/framework/param_spec.h>
#include <gis/framework/result.h>

#include <QApplication>
#include <QPainter>
#include <QPixmap>
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
#include <QSizePolicy>
#include <QStyle>

#include <array>

namespace {

QString cardIconText(ParamCardWidget::CardType type) {
    switch (type) {
    case ParamCardWidget::CardType::Input:
        return QStringLiteral("I");
    case ParamCardWidget::CardType::Output:
        return QStringLiteral("O");
    case ParamCardWidget::CardType::Advanced:
        return QStringLiteral("A");
    }
    return QStringLiteral("P");
}

QPixmap cardIconPixmap(const QString& text) {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#2F7CF6"));
    pen.setWidthF(1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    if (text == QStringLiteral("I")) {
        painter.drawRect(QRectF(3.5, 3.5, 9, 9));
        painter.drawLine(QPointF(5, 10.5), QPointF(7.5, 8));
        painter.drawLine(QPointF(7.5, 8), QPointF(9.2, 9.5));
        painter.drawLine(QPointF(9.2, 9.5), QPointF(11.2, 6.5));
    } else if (text == QStringLiteral("O")) {
        painter.drawLine(QPointF(4, 8), QPointF(12, 8));
        painter.drawLine(QPointF(9, 5), QPointF(12, 8));
        painter.drawLine(QPointF(9, 11), QPointF(12, 8));
    } else if (text == QStringLiteral("A")) {
        painter.drawLine(QPointF(4, 5), QPointF(12, 5));
        painter.drawLine(QPointF(4, 8), QPointF(12, 8));
        painter.drawLine(QPointF(4, 11), QPointF(12, 11));
        painter.drawEllipse(QRectF(6.5, 4, 3, 3));
        painter.drawEllipse(QRectF(9, 7, 3, 3));
        painter.drawEllipse(QRectF(5, 10, 3, 3));
    }
    return pixmap;
}

QIcon browseIcon() {
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#7E92A8"));
    pen.setWidthF(1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(1.5, 1.5, 11, 11), 2.2, 2.2);
    painter.drawLine(QPointF(7, 4), QPointF(7, 10));
    painter.drawLine(QPointF(4, 7), QPointF(10, 7));
    return QIcon(pixmap);
}

}

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
    cardContentLayout_->setSpacing(14);

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(10);

    auto* accentBar = new QFrame;
    accentBar->setObjectName(QStringLiteral("accentBar"));
    accentBar->setFixedHeight(20);
    headerLayout->addWidget(accentBar, 0, Qt::AlignTop);

    iconLabel_ = new QLabel;
    iconLabel_->setObjectName(QStringLiteral("cardIcon"));
    iconLabel_->setPixmap(cardIconPixmap(cardIconText(cardType_)));
    headerLayout->addWidget(iconLabel_, 0, Qt::AlignTop);

    static const QMap<CardType, QString> kDefaultTitles = {
        {CardType::Input,    QStringLiteral("输入参数")},
        {CardType::Output,   QStringLiteral("输出参数")},
        {CardType::Advanced, QStringLiteral("高级参数")},
    };

    titleLabel_ = new QLabel(kDefaultTitles.value(cardType_));
    titleLabel_->setObjectName(QStringLiteral("cardTitle"));
    headerLayout->addWidget(titleLabel_);
    headerLayout->addStretch();

    cardContentLayout_->addLayout(headerLayout);

    auto* paramsContainer = new QWidget;
    paramsLayout_ = new QGridLayout(paramsContainer);
    paramsLayout_->setContentsMargins(0, 0, 0, 0);
    paramsLayout_->setHorizontalSpacing(16);
    paramsLayout_->setVerticalSpacing(14);
    paramsLayout_->setColumnMinimumWidth(0, 220);
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
    labelWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    labelWidget->setMaximumWidth(220);
    auto* labelLayout = new QVBoxLayout(labelWidget);
    labelLayout->setContentsMargins(0, 0, 0, 0);
    labelLayout->setSpacing(2);

    auto* nameLabel = new QLabel(QString::fromUtf8(spec.displayName));
    nameLabel->setObjectName(QStringLiteral("paramLabel"));
    nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    labelLayout->addWidget(nameLabel);

    if (!spec.description.empty()) {
        auto* descLabel = new QLabel(QString::fromUtf8(spec.description));
        descLabel->setObjectName(QStringLiteral("paramDesc"));
        descLabel->setWordWrap(true);
        descLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        descLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        descLabel->setMaximumWidth(220);
        labelLayout->addWidget(descLabel);
    }

    if (spec.required) {
        auto* reqLabel = new QLabel(QStringLiteral("必填"));
        reqLabel->setObjectName(QStringLiteral("paramKey"));
        labelLayout->addWidget(reqLabel, 0, Qt::AlignLeft);
    }

    QWidget* inputWidget = createParamWidget(spec, entry);

    paramsLayout_->addWidget(labelWidget, paramsRow_, 0, Qt::AlignTop);
    paramsLayout_->addWidget(inputWidget, paramsRow_, 1, Qt::AlignTop);
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
            ? QStringLiteral("请输入 EPSG 代码，例如 EPSG:3857")
            : QStringLiteral("请选择文件或输入路径"));
    if (auto* defStr = std::get_if<std::string>(&spec.defaultValue); defStr && !defStr->empty()) {
        lineEdit->setText(QString::fromUtf8(*defStr));
    }
    entry.lineEdit = lineEdit;
    connect(lineEdit, &QLineEdit::textChanged, this, &ParamCardWidget::paramChanged);
    layout->addWidget(lineEdit, 1);

    if (spec.type != gis::framework::ParamType::CRS) {
        auto* browseBtn = new QPushButton(QStringLiteral("浏览"));
        browseBtn->setObjectName(QStringLiteral("browseButton"));
        browseBtn->setIcon(browseIcon());
        browseBtn->setIconSize(QSize(14, 14));
        entry.browseButton = browseBtn;

        bool isOutput = (spec.key == "output" || spec.key.find("output") != std::string::npos);
        bool isDir = (spec.type == gis::framework::ParamType::DirPath);
        connect(browseBtn, &QPushButton::clicked, this, [lineEdit, isOutput, isDir]() {
            QString filePath;
            if (isDir) {
                filePath = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("选择目录"));
            } else if (isOutput) {
                filePath = QFileDialog::getSaveFileName(nullptr, QStringLiteral("保存文件"), QString(),
                    QStringLiteral("所有文件 (*);;GeoTIFF (*.tif *.tiff);;Shapefile (*.shp)"));
            } else {
                filePath = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择文件"), QString(),
                    QStringLiteral("所有文件 (*);;GeoTIFF (*.tif *.tiff);;Shapefile (*.shp);;JPEG (*.jpg *.jpeg);;PNG (*.png)"));
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
    entry.intSpinBox = spinBox;
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
    auto* checkBox = new QCheckBox(QStringLiteral("启用"));
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
            : QStringLiteral("请输入参数值"));
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
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(label, i / 2, (i % 2) * 2);

        auto* spin = new QDoubleSpinBox;
        spin->setRange(-1e15, 1e15);
        spin->setDecimals(6);
        spin->setPrefix(QString::fromUtf8(kExtentLabels[i]) + QStringLiteral(" "));
        spin->setValue(defaultExtent[i]);
        spin->setMinimumHeight(gis::style::Size::kInputMinHeight);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ParamCardWidget::paramChanged);
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
        } else if (entry.spinBox) {
            result[key] = gis::framework::ParamValue(entry.spinBox->value());
        } else if (entry.intSpinBox) {
            result[key] = gis::framework::ParamValue(entry.intSpinBox->value());
        } else if (entry.checkBox) {
            result[key] = gis::framework::ParamValue(entry.checkBox->isChecked());
        } else if (entry.widget) {
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
    } else if (entry.spinBox) {
        entry.spinBox->setStyleSheet(error ? errorStyle : normalStyle);
    } else if (entry.intSpinBox) {
        entry.intSpinBox->setStyleSheet(error ? errorStyle : normalStyle);
    }
}

bool ParamCardWidget::hasParam(const std::string& key) const {
    return entries_.contains(key);
}

void ParamCardWidget::setStringValue(const std::string& key, const std::string& value) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return;
    }

    auto& entry = it.value();
    if (entry.lineEdit) {
        entry.lineEdit->setText(QString::fromUtf8(value));
        return;
    }

    if (entry.comboBox) {
        const QString text = QString::fromUtf8(value);
        const int index = entry.comboBox->findText(text);
        if (index >= 0) {
            entry.comboBox->setCurrentIndex(index);
        }
    }
}

bool ParamCardWidget::setValueFromString(const std::string& key, const std::string& value) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return false;
    }

    auto& entry = it.value();
    if (entry.lineEdit) {
        entry.lineEdit->setText(QString::fromUtf8(value));
        return true;
    }

    if (entry.comboBox) {
        const QString text = QString::fromUtf8(value);
        int index = entry.comboBox->findText(text);
        if (index < 0) {
            index = entry.comboBox->findText(text, Qt::MatchContains);
        }
        if (index >= 0) {
            entry.comboBox->setCurrentIndex(index);
            return true;
        }
        return false;
    }

    if (entry.spinBox) {
        bool ok = false;
        const double number = QString::fromUtf8(value).toDouble(&ok);
        if (!ok) {
            return false;
        }
        entry.spinBox->setValue(number);
        return true;
    }

    if (entry.intSpinBox) {
        bool ok = false;
        const int number = QString::fromUtf8(value).toInt(&ok);
        if (!ok) {
            return false;
        }
        entry.intSpinBox->setValue(number);
        return true;
    }

    if (entry.checkBox) {
        const QString text = QString::fromUtf8(value).trimmed().toLower();
        entry.checkBox->setChecked(
            text == QStringLiteral("1") ||
            text == QStringLiteral("true") ||
            text == QStringLiteral("yes") ||
            text == QStringLiteral("on"));
        return true;
    }

    if (entry.widget) {
        const QStringList parts = QString::fromUtf8(value).split(',', Qt::SkipEmptyParts);
        if (parts.size() != 4) {
            return false;
        }
        std::array<double, 4> extent = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            bool ok = false;
            extent[static_cast<size_t>(i)] = parts[i].trimmed().toDouble(&ok);
            if (!ok) {
                return false;
            }
        }
        setExtentValue(key, extent);
        return true;
    }

    return false;
}

void ParamCardWidget::setExtentValue(const std::string& key, const std::array<double, 4>& value) {
    auto it = entries_.find(key);
    if (it == entries_.end() || !it.value().widget) {
        return;
    }

    const auto spins = it.value().widget->findChildren<QDoubleSpinBox*>();
    if (spins.size() < 4) {
        return;
    }

    for (int i = 0; i < 4; ++i) {
        spins[i]->setValue(value[static_cast<size_t>(i)]);
    }
}


#include "param_card_widget.h"
#include "custom_index_preset_store.h"
#include "style_constants.h"
#include "gui_data_support.h"

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
#include <QListView>
#include <QPalette>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QInputDialog>
#include <QMessageBox>
#include <QStringList>

#include <array>
#include <map>

namespace {

QString enumDisplayText(const std::string& key, const std::string& value) {
    static const std::map<std::string, std::map<std::string, QString>> kLabels = {
        {"method", {
            {"binary", QStringLiteral("二值化")},
            {"binary_inv", QStringLiteral("反向二值化")},
            {"truncate", QStringLiteral("截断")},
            {"tozero", QStringLiteral("低值置零")},
            {"otsu", QStringLiteral("大津法")},
            {"adaptive_gaussian", QStringLiteral("自适应高斯")},
            {"adaptive_mean", QStringLiteral("自适应均值")},
            {"sift", QStringLiteral("SIFT 特征")},
            {"orb", QStringLiteral("ORB 特征")},
            {"akaze", QStringLiteral("AKAZE 特征")}
        }},
        {"match_method", {
            {"bf", QStringLiteral("暴力匹配")},
            {"flann", QStringLiteral("FLANN 近邻匹配")},
            {"sqdiff", QStringLiteral("平方差")},
            {"sqdiff_normed", QStringLiteral("归一化平方差")},
            {"ccorr", QStringLiteral("相关性")},
            {"ccorr_normed", QStringLiteral("归一化相关性")},
            {"ccoeff", QStringLiteral("相关系数")},
            {"ccoeff_normed", QStringLiteral("归一化相关系数")}
        }},
        {"transform", {
            {"affine", QStringLiteral("仿射")},
            {"projective", QStringLiteral("投影")},
            {"similarity", QStringLiteral("相似")},
            {"translation", QStringLiteral("平移")}
        }},
        {"resample", {
            {"nearest", QStringLiteral("最近邻")},
            {"bilinear", QStringLiteral("双线性")},
            {"cubic", QStringLiteral("三次卷积")},
            {"cubicspline", QStringLiteral("三次样条")},
            {"lanczos", QStringLiteral("Lanczos")},
            {"average", QStringLiteral("均值")},
            {"mode", QStringLiteral("众数")},
            {"gaussian", QStringLiteral("高斯")}
        }},
        {"change_method", {
            {"differencing", QStringLiteral("差值法")},
            {"ratio", QStringLiteral("比值法")},
            {"pcd", QStringLiteral("主成分差分")}
        }},
        {"ecc_motion", {
            {"translation", QStringLiteral("平移")},
            {"euclidean", QStringLiteral("欧式")},
            {"affine", QStringLiteral("仿射")},
            {"homography", QStringLiteral("单应")}
        }},
        {"corner_method", {
            {"harris", QStringLiteral("Harris")},
            {"shi_tomasi", QStringLiteral("Shi-Tomasi")}
        }},
        {"filter_type", {
            {"gaussian", QStringLiteral("高斯滤波")},
            {"median", QStringLiteral("中值滤波")},
            {"bilateral", QStringLiteral("双边滤波")},
            {"morph_open", QStringLiteral("开运算")},
            {"morph_close", QStringLiteral("闭运算")},
            {"morph_dilate", QStringLiteral("膨胀")},
            {"morph_erode", QStringLiteral("腐蚀")}
        }},
        {"enhance_type", {
            {"equalize", QStringLiteral("直方图均衡化")},
            {"clahe", QStringLiteral("CLAHE")},
            {"normalize", QStringLiteral("归一化")},
            {"log", QStringLiteral("对数增强")},
            {"gamma", QStringLiteral("Gamma 校正")}
        }},
        {"edge_method", {
            {"canny", QStringLiteral("Canny 边缘")},
            {"sobel", QStringLiteral("Sobel 边缘")},
            {"laplacian", QStringLiteral("Laplacian 边缘")},
            {"scharr", QStringLiteral("Scharr 边缘")}
        }},
        {"pan_method", {
            {"brovey", QStringLiteral("Brovey 融合")},
            {"simple_mean", QStringLiteral("简单均值")},
            {"ihs", QStringLiteral("IHS 融合")}
        }},
        {"hough_type", {
            {"lines", QStringLiteral("直线检测")},
            {"circles", QStringLiteral("圆检测")}
        }},
        {"cmap", {
            {"jet", QStringLiteral("伪彩虹")},
            {"viridis", QStringLiteral("Viridis")},
            {"hot", QStringLiteral("热度")},
            {"cool", QStringLiteral("冷色")},
            {"spring", QStringLiteral("春季")},
            {"summer", QStringLiteral("夏季")},
            {"autumn", QStringLiteral("秋季")},
            {"winter", QStringLiteral("冬季")},
            {"bone", QStringLiteral("骨架")},
            {"hsv", QStringLiteral("HSV 色环")},
            {"rainbow", QStringLiteral("彩虹")},
            {"ocean", QStringLiteral("海洋")}
        }},
        {"format", {
            {"GeoJSON", QStringLiteral("GeoJSON 格式")},
            {"ESRI Shapefile", QStringLiteral("Shapefile 格式")},
            {"GPKG", QStringLiteral("GeoPackage 格式")},
            {"KML", QStringLiteral("KML 格式")},
            {"CSV", QStringLiteral("CSV 表格")}
        }},
        {"preset", {
            {"none", QStringLiteral("手动输入")},
            {"ndvi_alias", QStringLiteral("NDVI 示例")},
            {"ndwi_alias", QStringLiteral("NDWI 示例")},
            {"mndwi_alias", QStringLiteral("MNDWI 示例")},
            {"ndbi_alias", QStringLiteral("NDBI 示例")},
            {"gndvi_alias", QStringLiteral("GNDVI 示例")},
            {"savi_alias", QStringLiteral("SAVI 示例")},
            {"evi_alias", QStringLiteral("EVI 示例")}
        }}
    };

    const auto keyIt = kLabels.find(key);
    if (keyIt == kLabels.end()) {
        return QString::fromUtf8(value);
    }

    const auto valueIt = keyIt->second.find(value);
    if (valueIt == keyIt->second.end()) {
        return QString::fromUtf8(value);
    }

    return QStringLiteral("%1 (%2)")
        .arg(QString::fromUtf8(value))
        .arg(valueIt->second);
}

class ComboPopupItemDelegate : public QStyledItemDelegate {
public:
    explicit ComboPopupItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();

        const bool selected = (option.state & QStyle::State_Selected) != 0;
        const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
        const QColor background = selected
            ? QColor(gis::style::Color::kPrimaryLight)
            : (hovered ? QColor("#F3F7FC") : QColor(gis::style::Color::kCardBg));

        painter->fillRect(option.rect, background);

        QRect textRect = option.rect.adjusted(12, 0, -12, 0);
        painter->setPen(QColor(gis::style::Color::kTextPrimary));
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          QFontMetrics(option.font).elidedText(
                              index.data(Qt::DisplayRole).toString(),
                              Qt::ElideRight,
                              textRect.width()));

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 32));
        return size;
    }
};

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

bool usesMultiFileTextPicker(const std::string& pluginName,
                             const std::string& actionKey,
                             const std::string& paramKey) {
    return (pluginName == "classification" && actionKey == "feature_stats" && paramKey == "rasters")
        || (pluginName == "cutting" && actionKey == "merge_bands" && paramKey == "bands");
}

QString multiFileTextPickerFilter(const std::string& pluginName,
                                  const std::string& actionKey,
                                  const std::string& paramKey) {
    Q_UNUSED(pluginName);
    Q_UNUSED(actionKey);
    Q_UNUSED(paramKey);
    return QStringLiteral(
        "栅格文件 (*.tif *.tiff *.img *.vrt *.png *.jpg *.jpeg *.bmp);;"
        "GeoTIFF (*.tif *.tiff);;IMG (*.img);;VRT (*.vrt);;"
        "JPEG (*.jpg *.jpeg);;PNG (*.png);;BMP (*.bmp);;所有文件 (*)");
}

bool isCustomIndexPresetEnum(const std::string& pluginName,
                             const std::string& actionKey,
                             const std::string& paramKey) {
    return pluginName == "spindex" && actionKey == "custom_index" && paramKey == "preset";
}

void populateCustomIndexPresetCombo(QComboBox* comboBox) {
    comboBox->clear();
    for (const auto& value : gis::gui::spindexCustomIndexPresetValues()) {
        QString label = enumDisplayText("preset", value);
        if (gis::gui::isCustomIndexUserPresetKey(value)) {
            label = QStringLiteral("%1 (自定义)")
                        .arg(QString::fromUtf8(gis::gui::findCustomIndexUserPresetName(value)));
        }
        comboBox->addItem(label, QString::fromUtf8(value));
    }
}

}

ParamCardWidget::ParamCardWidget(CardType type, QWidget* parent)
    : QWidget(parent), cardType_(type) {
    setupUi();
}

void ParamCardWidget::setUiContext(const std::string& pluginName, const std::string& actionKey) {
    pluginName_ = pluginName;
    actionKey_ = actionKey;
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
    const auto uiConfig = gis::gui::buildFileParamUiConfig(pluginName_, actionKey_, spec.key, spec.type);
    if (!uiConfig.placeholder.empty()) {
        lineEdit->setPlaceholderText(QString::fromUtf8(uiConfig.placeholder));
    } else {
        lineEdit->setPlaceholderText(
            spec.type == gis::framework::ParamType::CRS
                ? QStringLiteral("请输入 EPSG 代码，例如 EPSG:3857")
                : QStringLiteral("请选择文件或输入路径"));
    }
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

        bool isOutput = uiConfig.isOutput || (spec.key == "output" || spec.key.find("output") != std::string::npos);
        bool isDir = uiConfig.selectDirectory || (spec.type == gis::framework::ParamType::DirPath);
        const QString openFilter = uiConfig.openFilter.empty()
            ? QStringLiteral("所有文件 (*);;GeoTIFF (*.tif *.tiff);;Shapefile (*.shp);;JPEG (*.jpg *.jpeg);;PNG (*.png)")
            : QString::fromUtf8(uiConfig.openFilter);
        const QString saveFilter = uiConfig.saveFilter.empty()
            ? QStringLiteral("所有文件 (*);;GeoTIFF (*.tif *.tiff);;Shapefile (*.shp)")
            : QString::fromUtf8(uiConfig.saveFilter);
        const QString defaultSuffix = QString::fromUtf8(uiConfig.suggestedSuffix);
        const bool allowMultiSelect = uiConfig.allowMultiSelect;
        connect(browseBtn, &QPushButton::clicked, this, [lineEdit, isOutput, isDir, allowMultiSelect, openFilter, saveFilter, defaultSuffix]() {
            QString filePath;
            if (isDir) {
                filePath = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("选择目录"));
            } else if (isOutput) {
                QFileDialog dialog(nullptr, QStringLiteral("保存文件"));
                dialog.setAcceptMode(QFileDialog::AcceptSave);
                dialog.setNameFilter(saveFilter);
                dialog.selectNameFilter(saveFilter.section(QStringLiteral(";;"), 0, 0));
                if (!defaultSuffix.isEmpty()) {
                    dialog.setDefaultSuffix(defaultSuffix);
                }
                if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) {
                    filePath = dialog.selectedFiles().front();
                }
            } else {
                if (allowMultiSelect) {
                    const QStringList files = QFileDialog::getOpenFileNames(
                        nullptr, QStringLiteral("选择文件"), QString(), openFilter);
                    if (!files.isEmpty()) {
                        filePath = files.join(QStringLiteral(","));
                    }
                } else {
                    filePath = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择文件"), QString(), openFilter);
                }
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
    auto* listView = new QListView(comboBox);
    listView->setItemDelegate(new ComboPopupItemDelegate(listView));
    QPalette palette = listView->palette();
    palette.setColor(QPalette::Base, QColor(gis::style::Color::kCardBg));
    palette.setColor(QPalette::Text, QColor(gis::style::Color::kTextPrimary));
    palette.setColor(QPalette::Highlight, QColor(gis::style::Color::kPrimaryLight));
    palette.setColor(QPalette::HighlightedText, QColor(gis::style::Color::kTextPrimary));
    listView->setPalette(palette);
    listView->setStyleSheet(QStringLiteral(
        "QListView {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  outline: none;"
        "  padding: 4px 0;"
        "}"
        "QListView::item { min-height: 28px; }")
        .arg(gis::style::Color::kCardBg)
        .arg(gis::style::Color::kTextPrimary)
        .arg(gis::style::Color::kInputBorder));
    comboBox->setView(listView);
    if (isCustomIndexPresetEnum(pluginName_, actionKey_, spec.key)) {
        populateCustomIndexPresetCombo(comboBox);
    } else {
        for (const auto& val : spec.enumValues) {
            comboBox->addItem(enumDisplayText(spec.key, val), QString::fromUtf8(val));
        }
    }
    if (auto* defStr = std::get_if<std::string>(&spec.defaultValue); defStr && !defStr->empty()) {
        int idx = comboBox->findData(QString::fromUtf8(*defStr));
        if (idx >= 0) comboBox->setCurrentIndex(idx);
    }
    entry.comboBox = comboBox;
    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamCardWidget::paramChanged);
    if (!isCustomIndexPresetEnum(pluginName_, actionKey_, spec.key)) {
        return comboBox;
    }

    auto* container = new QWidget;
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(comboBox, 1);

    auto* saveButton = new QPushButton(QStringLiteral("保存当前"));
    saveButton->setObjectName(QStringLiteral("browseButton"));
    entry.auxButton = saveButton;
    layout->addWidget(saveButton);

    auto* deleteButton = new QPushButton(QStringLiteral("删除预设"));
    deleteButton->setObjectName(QStringLiteral("browseButton"));
    entry.secondaryAuxButton = deleteButton;
    layout->addWidget(deleteButton);

    connect(saveButton, &QPushButton::clicked, this, [this, comboBox]() {
        const auto expressionIt = entries_.find("expression");
        if (expressionIt == entries_.end() || !expressionIt.value().lineEdit) {
            QMessageBox::warning(this, QStringLiteral("保存预设"), QStringLiteral("当前界面没有可保存的表达式"));
            return;
        }

        const QString expression = expressionIt.value().lineEdit->text().trimmed();
        if (expression.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("保存预设"), QStringLiteral("请先填写表达式"));
            return;
        }

        QString defaultName;
        const std::string currentKey = comboBox->currentData().toString().toUtf8().constData();
        if (gis::gui::isCustomIndexUserPresetKey(currentKey)) {
            defaultName = QString::fromUtf8(gis::gui::findCustomIndexUserPresetName(currentKey));
        }

        bool accepted = false;
        const QString name = QInputDialog::getText(
            this,
            QStringLiteral("保存预设"),
            QStringLiteral("请输入预设名称"),
            QLineEdit::Normal,
            defaultName,
            &accepted).trimmed();
        if (!accepted) {
            return;
        }
        if (name.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("保存预设"), QStringLiteral("预设名称不能为空"));
            return;
        }

        std::string errorMessage;
        const std::string savedKey = gis::gui::saveCustomIndexUserPreset(
            name.toUtf8().constData(),
            expression.toUtf8().constData(),
            &errorMessage);
        if (savedKey.empty()) {
            QMessageBox::warning(this, QStringLiteral("保存预设"), QString::fromUtf8(errorMessage));
            return;
        }

        populateCustomIndexPresetCombo(comboBox);
        const int index = comboBox->findData(QString::fromUtf8(savedKey));
        if (index >= 0) {
            comboBox->setCurrentIndex(index);
        }
        emit paramChanged();
    });

    connect(deleteButton, &QPushButton::clicked, this, [this, comboBox]() {
        const std::string currentKey = comboBox->currentData().toString().toUtf8().constData();
        if (!gis::gui::isCustomIndexUserPresetKey(currentKey)) {
            QMessageBox::warning(this, QStringLiteral("删除预设"), QStringLiteral("当前预设不是自定义预设"));
            return;
        }

        if (QMessageBox::question(
                this,
                QStringLiteral("删除预设"),
                QStringLiteral("确定删除当前自定义预设吗？")) != QMessageBox::Yes) {
            return;
        }

        std::string errorMessage;
        if (!gis::gui::removeCustomIndexUserPreset(currentKey, &errorMessage)) {
            QMessageBox::warning(this, QStringLiteral("删除预设"), QString::fromUtf8(errorMessage));
            return;
        }

        populateCustomIndexPresetCombo(comboBox);
        const int noneIndex = comboBox->findData(QStringLiteral("none"));
        if (noneIndex >= 0) {
            comboBox->setCurrentIndex(noneIndex);
        }
        emit paramChanged();
    });

    return container;
}

QWidget* ParamCardWidget::createIntWidget(const gis::framework::ParamSpec& spec,
                                           ParamWidgetEntry& entry) {
    auto* spinBox = new QSpinBox;
    spinBox->setRange(-2147483647, 2147483647);

    const auto* minInt = std::get_if<int>(&spec.minValue);
    const auto* maxInt = std::get_if<int>(&spec.maxValue);
    if (minInt && maxInt) {
        if (*maxInt > *minInt) {
            spinBox->setRange(*minInt, *maxInt);
        } else if (*minInt != 0) {
            spinBox->setMinimum(*minInt);
        } else if (*maxInt != 0) {
            spinBox->setMaximum(*maxInt);
        }
    } else {
        if (minInt && *minInt != 0) {
            spinBox->setMinimum(*minInt);
        }
        if (maxInt && *maxInt != 0) {
            spinBox->setMaximum(*maxInt);
        }
    }
    if (auto* defInt = std::get_if<int>(&spec.defaultValue)) {
        spinBox->setValue(*defInt);
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
    if (auto* minDouble = std::get_if<double>(&spec.minValue)) {
        spinBox->setMinimum(*minDouble);
    }
    if (auto* maxDouble = std::get_if<double>(&spec.maxValue)) {
        spinBox->setMaximum(*maxDouble);
    }
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
    if (usesMultiFileTextPicker(pluginName_, actionKey_, spec.key)) {
        auto* container = new QWidget;
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        auto* lineEdit = new QLineEdit;
        const std::string placeholder =
            gis::gui::buildTextParamPlaceholder(pluginName_, actionKey_, spec);
        lineEdit->setPlaceholderText(
            !placeholder.empty()
                ? QString::fromUtf8(placeholder)
                : QStringLiteral("请输入参数值"));
        if (auto* defStr = std::get_if<std::string>(&spec.defaultValue); defStr && !defStr->empty()) {
            lineEdit->setText(QString::fromUtf8(*defStr));
        }
        entry.lineEdit = lineEdit;
        connect(lineEdit, &QLineEdit::textChanged, this, &ParamCardWidget::paramChanged);
        layout->addWidget(lineEdit, 1);

        auto* browseBtn = new QPushButton(QStringLiteral("浏览"));
        browseBtn->setObjectName(QStringLiteral("browseButton"));
        browseBtn->setIcon(browseIcon());
        browseBtn->setIconSize(QSize(14, 14));
        const QString filter = multiFileTextPickerFilter(pluginName_, actionKey_, spec.key);
        connect(browseBtn, &QPushButton::clicked, this, [lineEdit, filter]() {
            const QStringList files = QFileDialog::getOpenFileNames(
                nullptr, QStringLiteral("选择文件"), QString(), filter);
            if (!files.isEmpty()) {
                lineEdit->setText(files.join(QStringLiteral(",")));
            }
        });
        layout->addWidget(browseBtn);
        return container;
    }

    auto* lineEdit = new QLineEdit;
    const std::string placeholder =
        gis::gui::buildTextParamPlaceholder(pluginName_, actionKey_, spec);
    lineEdit->setPlaceholderText(
        !placeholder.empty()
            ? QString::fromUtf8(placeholder)
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
            result[key] = gis::framework::ParamValue(entry.comboBox->currentData().toString().toUtf8().constData());
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
        int index = entry.comboBox->findData(text);
        if (index < 0 && isCustomIndexPresetEnum(pluginName_, actionKey_, key)) {
            populateCustomIndexPresetCombo(entry.comboBox);
            index = entry.comboBox->findData(text);
        }
        if (index < 0) {
            index = entry.comboBox->findText(text);
        }
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
        int index = entry.comboBox->findData(text);
        if (index < 0 && isCustomIndexPresetEnum(pluginName_, actionKey_, key)) {
            populateCustomIndexPresetCombo(entry.comboBox);
            index = entry.comboBox->findData(text);
        }
        if (index < 0) {
            index = entry.comboBox->findText(text);
        }
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


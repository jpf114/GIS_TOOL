#include "preview_panel.h"

#include "gui_data_support.h"

#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>

namespace {

struct DatasetCloser {
    void operator()(GDALDataset* ds) const {
        if (ds) {
            GDALClose(ds);
        }
    }
};

QString toQString(const std::string& text) {
    return QString::fromUtf8(text);
}

QString geometryTypeName(OGRwkbGeometryType type) {
    switch (wkbFlatten(type)) {
        case wkbPoint: return QStringLiteral("Point");
        case wkbMultiPoint: return QStringLiteral("MultiPoint");
        case wkbLineString: return QStringLiteral("LineString");
        case wkbMultiLineString: return QStringLiteral("MultiLineString");
        case wkbPolygon: return QStringLiteral("Polygon");
        case wkbMultiPolygon: return QStringLiteral("MultiPolygon");
        default: return QStringLiteral("Unknown");
    }
}

QImage buildRasterPreviewImage(GDALDataset* ds) {
    constexpr int kMaxPreviewSize = 640;

    const int srcWidth = ds->GetRasterXSize();
    const int srcHeight = ds->GetRasterYSize();
    if (srcWidth <= 0 || srcHeight <= 0 || ds->GetRasterCount() <= 0) {
        return {};
    }

    const int longEdge = std::max(srcWidth, srcHeight);
    const double scale = longEdge > kMaxPreviewSize
        ? static_cast<double>(kMaxPreviewSize) / static_cast<double>(longEdge)
        : 1.0;
    const int previewWidth = std::max(1, static_cast<int>(std::round(srcWidth * scale)));
    const int previewHeight = std::max(1, static_cast<int>(std::round(srcHeight * scale)));

    auto* band = ds->GetRasterBand(1);
    if (!band) {
        return {};
    }

    std::vector<float> buffer(previewWidth * previewHeight);
    if (band->RasterIO(GF_Read, 0, 0, srcWidth, srcHeight,
                       buffer.data(), previewWidth, previewHeight,
                       GDT_Float32, 0, 0) != CE_None) {
        return {};
    }

    int hasNoData = 0;
    const double noDataValue = band->GetNoDataValue(&hasNoData);

    float minValue = std::numeric_limits<float>::max();
    float maxValue = std::numeric_limits<float>::lowest();
    for (float value : buffer) {
        if (!std::isfinite(value)) {
            continue;
        }
        if (hasNoData && std::abs(value - static_cast<float>(noDataValue)) < 1e-6f) {
            continue;
        }
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }

    if (minValue == std::numeric_limits<float>::max() || maxValue <= minValue) {
        minValue = 0.0f;
        maxValue = 1.0f;
    }

    QImage image(previewWidth, previewHeight, QImage::Format_Grayscale8);
    for (int y = 0; y < previewHeight; ++y) {
        auto* line = image.scanLine(y);
        for (int x = 0; x < previewWidth; ++x) {
            const float value = buffer[y * previewWidth + x];
            int pixel = 0;
            if (std::isfinite(value) &&
                (!hasNoData || std::abs(value - static_cast<float>(noDataValue)) >= 1e-6f)) {
                const double normalized = (value - minValue) / (maxValue - minValue);
                pixel = static_cast<int>(std::clamp(normalized, 0.0, 1.0) * 255.0);
            }
            line[x] = static_cast<unsigned char>(pixel);
        }
    }

    return image;
}

QString rasterSummary(GDALDataset* ds) {
    std::ostringstream oss;
    oss << "尺寸: " << ds->GetRasterXSize() << " x " << ds->GetRasterYSize() << "\n";
    oss << "波段: " << ds->GetRasterCount() << "\n";
    if (ds->GetDriver()) {
        oss << "格式: " << ds->GetDriver()->GetDescription() << "\n";
    }
    const char* projection = ds->GetProjectionRef();
    if (projection && projection[0] != '\0') {
        OGRSpatialReference srs;
        if (srs.importFromWkt(projection) == OGRERR_NONE) {
            const char* authName = srs.GetAuthorityName(nullptr);
            const char* authCode = srs.GetAuthorityCode(nullptr);
            if (authName && authCode) {
                oss << "坐标系: " << authName << ":" << authCode << "\n";
            }
        }
    }
    return toQString(oss.str());
}

QString vectorSummary(GDALDataset* ds) {
    std::ostringstream oss;
    oss << "图层数: " << ds->GetLayerCount() << "\n";
    if (ds->GetDriver()) {
        oss << "格式: " << ds->GetDriver()->GetDescription() << "\n";
    }

    if (auto* layer = ds->GetLayer(0)) {
        oss << "首图层: " << layer->GetName() << "\n";
        oss << "几何: " << geometryTypeName(layer->GetGeomType()).toStdString() << "\n";
        oss << "要素: " << layer->GetFeatureCount(TRUE) << "\n";
    }
    return toQString(oss.str());
}

} // namespace

PreviewPanel::PreviewPanel(QWidget* parent)
    : QWidget(parent) {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* headerFrame = new QFrame;
    headerFrame->setObjectName(QStringLiteral("previewHeader"));
    auto* headerLayout = new QVBoxLayout(headerFrame);
    headerLayout->setContentsMargins(16, 14, 16, 14);
    headerLayout->setSpacing(6);

    titleLabel_ = new QLabel(QStringLiteral("预览区"));
    titleLabel_->setStyleSheet("font-size: 18px; font-weight: 600;");
    pathLabel_ = new QLabel(QStringLiteral("未选择数据"));
    pathLabel_->setWordWrap(true);
    pathLabel_->setStyleSheet("color: #5f6b7a;");
    summaryLabel_ = new QLabel;
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setStyleSheet("color: #2f3a46;");

    headerLayout->addWidget(titleLabel_);
    headerLayout->addWidget(pathLabel_);
    headerLayout->addWidget(summaryLabel_);

    stackedWidget_ = new QStackedWidget;

    placeholderLabel_ = new QLabel(QStringLiteral("请选择左侧数据，或执行算法后查看输出预览"));
    placeholderLabel_->setAlignment(Qt::AlignCenter);
    placeholderLabel_->setWordWrap(true);
    placeholderLabel_->setStyleSheet(
        "border: 1px dashed #b8c2cc; border-radius: 10px; color: #607080; "
        "background: #f7f9fb; font-size: 16px; padding: 24px;");

    imageLabel_ = new QLabel;
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setMinimumSize(480, 360);
    imageLabel_->setStyleSheet("background: #111827; border-radius: 10px;");

    auto* imageScrollArea = new QScrollArea;
    imageScrollArea->setWidgetResizable(true);
    imageScrollArea->setAlignment(Qt::AlignCenter);
    imageScrollArea->setWidget(imageLabel_);

    stackedWidget_->addWidget(placeholderLabel_);
    stackedWidget_->addWidget(imageScrollArea);

    rootLayout->addWidget(headerFrame);
    rootLayout->addWidget(stackedWidget_, 1);

    setStyleSheet(
        "QFrame#previewHeader {"
        "  background: #eef4f8;"
        "  border: 1px solid #d8e2ea;"
        "  border-radius: 10px;"
        "}");

    clearPreview();
}

void PreviewPanel::clearPreview() {
    setPlaceholder(QStringLiteral("预览区"),
                   QStringLiteral("请选择左侧数据，或执行算法后查看输出预览"));
}

void PreviewPanel::showPath(const std::string& path) {
    switch (gis::gui::detectDataKind(path)) {
        case gis::gui::DataKind::Raster:
            showRasterPreview(path);
            break;
        case gis::gui::DataKind::Vector:
            showVectorPreview(path);
            break;
        default:
            showUnsupportedPreview(path);
            break;
    }
}

void PreviewPanel::setPlaceholder(const QString& title, const QString& message) {
    titleLabel_->setText(title);
    pathLabel_->setText(QStringLiteral("暂无可展示文件"));
    summaryLabel_->setText(message);
    placeholderLabel_->setText(message);
    stackedWidget_->setCurrentIndex(0);
}

void PreviewPanel::setSummary(const QString& title, const QString& summary, const QString& pathText) {
    titleLabel_->setText(title);
    pathLabel_->setText(pathText);
    summaryLabel_->setText(summary);
}

void PreviewPanel::showRasterPreview(const std::string& path) {
    std::unique_ptr<GDALDataset, DatasetCloser> ds(
        static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly)));
    if (!ds) {
        setPlaceholder(QStringLiteral("栅格预览"),
                       QStringLiteral("无法打开栅格数据"));
        return;
    }

    QImage image = buildRasterPreviewImage(ds.get());
    if (image.isNull()) {
        setPlaceholder(QStringLiteral("栅格预览"),
                       QStringLiteral("栅格已加载，但暂时无法生成预览图"));
        return;
    }

    setSummary(QStringLiteral("栅格预览"),
               rasterSummary(ds.get()),
               toQString(path));
    imageLabel_->setPixmap(QPixmap::fromImage(image));
    stackedWidget_->setCurrentIndex(1);
}

void PreviewPanel::showVectorPreview(const std::string& path) {
    std::unique_ptr<GDALDataset, DatasetCloser> ds(
        static_cast<GDALDataset*>(GDALOpenEx(path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                             nullptr, nullptr, nullptr)));
    if (!ds) {
        setPlaceholder(QStringLiteral("矢量预览"),
                       QStringLiteral("无法打开矢量数据"));
        return;
    }

    setSummary(QStringLiteral("矢量预览"),
               vectorSummary(ds.get()),
               toQString(path));
    placeholderLabel_->setText(QStringLiteral("当前为轻量预览模式\n\n矢量数据先展示摘要信息，后续可升级为真实地图浏览。"));
    stackedWidget_->setCurrentIndex(0);
}

void PreviewPanel::showUnsupportedPreview(const std::string& path) {
    setSummary(QStringLiteral("预览区"),
               QStringLiteral("该文件类型暂不支持预览，但仍可作为输入或输出结果保存在数据区。"),
               toQString(path));
    placeholderLabel_->setText(QStringLiteral("暂不支持该类型的预览"));
    stackedWidget_->setCurrentIndex(0);
}

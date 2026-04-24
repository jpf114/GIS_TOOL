#include "preview_panel.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

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
    constexpr int kMaxPreviewSize = 960;

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

QString scaleLabelText(gis::gui::DataKind kind, double scale) {
    if (kind != gis::gui::DataKind::Raster) {
        return QStringLiteral("摘要模式");
    }
    return QStringLiteral("%1%").arg(static_cast<int>(std::round(scale * 100.0)));
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

    auto* toolbarLayout = new QHBoxLayout;
    toolbarLayout->setContentsMargins(0, 4, 0, 0);

    zoomOutButton_ = new QPushButton(QStringLiteral("缩小"));
    fitButton_ = new QPushButton(QStringLiteral("适配"));
    zoomInButton_ = new QPushButton(QStringLiteral("放大"));
    statusLabel_ = new QLabel;
    statusLabel_->setStyleSheet("color: #4b5c6f;");
    scaleLabel_ = new QLabel(QStringLiteral("100%"));
    scaleLabel_->setStyleSheet("color: #4b5c6f; font-weight: 600;");

    connect(zoomOutButton_, &QPushButton::clicked, this, [this]() {
        setScale(gis::gui::zoomOutScale(currentScale_), false);
    });
    connect(zoomInButton_, &QPushButton::clicked, this, [this]() {
        setScale(gis::gui::zoomInScale(currentScale_), false);
    });
    connect(fitButton_, &QPushButton::clicked, this, &PreviewPanel::refitPreview);

    toolbarLayout->addWidget(zoomOutButton_);
    toolbarLayout->addWidget(fitButton_);
    toolbarLayout->addWidget(zoomInButton_);
    toolbarLayout->addSpacing(8);
    toolbarLayout->addWidget(statusLabel_, 1);
    toolbarLayout->addWidget(scaleLabel_);

    headerLayout->addWidget(titleLabel_);
    headerLayout->addWidget(pathLabel_);
    headerLayout->addWidget(summaryLabel_);
    headerLayout->addLayout(toolbarLayout);

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

    imageScrollArea_ = new QScrollArea;
    imageScrollArea_->setWidgetResizable(true);
    imageScrollArea_->setAlignment(Qt::AlignCenter);
    imageScrollArea_->setWidget(imageLabel_);
    imageScrollArea_->viewport()->installEventFilter(this);
    imageLabel_->installEventFilter(this);

    stackedWidget_->addWidget(placeholderLabel_);
    stackedWidget_->addWidget(imageScrollArea_);

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
    currentDataKind_ = gis::gui::DataKind::Unknown;
    currentImage_ = QImage();
    currentScale_ = 1.0;
    fitMode_ = true;
    isPanning_ = false;
    lastPanPoint_ = {};
    if (imageLabel_) {
        imageLabel_->setCursor(Qt::ArrowCursor);
    }
    setZoomControlsEnabled(false);
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

void PreviewPanel::refitPreview() {
    fitCurrentImage();
}

void PreviewPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fitMode_ && hasRasterPreview()) {
        fitCurrentImage();
    }
}

bool PreviewPanel::eventFilter(QObject* watched, QEvent* event) {
    const bool previewReady = hasRasterPreview();
    if ((watched == imageScrollArea_->viewport() || watched == imageLabel_) && previewReady) {
        if (event->type() == QEvent::Wheel) {
            auto* wheelEvent = static_cast<QWheelEvent*>(event);
            if (wheelEvent->angleDelta().y() > 0) {
                setScale(gis::gui::zoomInScale(currentScale_), false);
            } else if (wheelEvent->angleDelta().y() < 0) {
                setScale(gis::gui::zoomOutScale(currentScale_), false);
            }
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                isPanning_ = true;
                lastPanPoint_ = mouseEvent->globalPosition().toPoint();
                imageLabel_->setCursor(Qt::ClosedHandCursor);
                updateScaleLabel();
                return true;
            }
        }

        if (event->type() == QEvent::MouseMove && isPanning_) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QPoint currentPoint = mouseEvent->globalPosition().toPoint();
            const QPoint delta = currentPoint - lastPanPoint_;
            lastPanPoint_ = currentPoint;

            imageScrollArea_->horizontalScrollBar()->setValue(
                imageScrollArea_->horizontalScrollBar()->value() - delta.x());
            imageScrollArea_->verticalScrollBar()->setValue(
                imageScrollArea_->verticalScrollBar()->value() - delta.y());
            return true;
        }

        if ((event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::Leave) && isPanning_) {
            isPanning_ = false;
            imageLabel_->setCursor(Qt::OpenHandCursor);
            updateScaleLabel();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void PreviewPanel::setPlaceholder(const QString& title, const QString& message) {
    titleLabel_->setText(title);
    pathLabel_->setText(QStringLiteral("暂无可展示文件"));
    summaryLabel_->setText(message);
    placeholderLabel_->setText(message);
    stackedWidget_->setCurrentIndex(0);
    updateScaleLabel();
}

void PreviewPanel::setSummary(const QString& title, const QString& summary, const QString& pathText) {
    titleLabel_->setText(title);
    pathLabel_->setText(pathText);
    summaryLabel_->setText(summary);
}

void PreviewPanel::showRasterPreview(const std::string& path) {
    currentDataKind_ = gis::gui::DataKind::Raster;
    std::unique_ptr<GDALDataset, DatasetCloser> ds(
        static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly)));
    if (!ds) {
        currentImage_ = QImage();
        setZoomControlsEnabled(false);
        setPlaceholder(QStringLiteral("栅格预览"),
                       QStringLiteral("无法打开栅格数据"));
        return;
    }

    currentImage_ = buildRasterPreviewImage(ds.get());
    if (currentImage_.isNull()) {
        setZoomControlsEnabled(false);
        setPlaceholder(QStringLiteral("栅格预览"),
                       QStringLiteral("栅格已加载，但暂时无法生成预览图"));
        return;
    }

    setSummary(QStringLiteral("栅格预览"),
               rasterSummary(ds.get()),
               toQString(path));
    setZoomControlsEnabled(true);
    imageLabel_->setCursor(Qt::OpenHandCursor);
    stackedWidget_->setCurrentIndex(1);
    fitCurrentImage();
}

void PreviewPanel::showVectorPreview(const std::string& path) {
    currentDataKind_ = gis::gui::DataKind::Vector;
    currentImage_ = QImage();
    currentScale_ = 1.0;
    fitMode_ = false;
    isPanning_ = false;
    setZoomControlsEnabled(false);
    imageLabel_->setCursor(Qt::ArrowCursor);

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
    placeholderLabel_->setText(QStringLiteral(
        "当前为轻量预览模式\n\n矢量数据先展示摘要信息，后续可升级为真实地图浏览。"));
    stackedWidget_->setCurrentIndex(0);
    updateScaleLabel();
}

void PreviewPanel::showUnsupportedPreview(const std::string& path) {
    currentDataKind_ = gis::gui::DataKind::Unknown;
    currentImage_ = QImage();
    currentScale_ = 1.0;
    fitMode_ = false;
    isPanning_ = false;
    setZoomControlsEnabled(false);
    imageLabel_->setCursor(Qt::ArrowCursor);
    setSummary(QStringLiteral("预览区"),
               QStringLiteral("该文件类型暂不支持预览，但仍可作为输入或输出结果保存在数据区。"),
               toQString(path));
    placeholderLabel_->setText(QStringLiteral("暂不支持该类型的预览"));
    stackedWidget_->setCurrentIndex(0);
    updateScaleLabel();
}

void PreviewPanel::updateImageDisplay() {
    if (currentImage_.isNull()) {
        imageLabel_->clear();
        updateScaleLabel();
        return;
    }

    const QSize scaledSize(
        std::max(1, static_cast<int>(std::round(currentImage_.width() * currentScale_))),
        std::max(1, static_cast<int>(std::round(currentImage_.height() * currentScale_))));

    const QPixmap pixmap = QPixmap::fromImage(currentImage_).scaled(
        scaledSize,
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation);
    imageLabel_->setPixmap(pixmap);
    imageLabel_->resize(pixmap.size());
    updateScaleLabel();
}

void PreviewPanel::updateScaleLabel() {
    scaleLabel_->setText(scaleLabelText(currentDataKind_, currentScale_));
    statusLabel_->setText(QString::fromUtf8(
        gis::gui::buildPreviewStatusText(currentDataKind_, currentScale_, fitMode_, isPanning_)));
}

void PreviewPanel::setScale(double scale, bool keepFitMode) {
    if (currentImage_.isNull()) {
        currentScale_ = 1.0;
        updateScaleLabel();
        return;
    }

    currentScale_ = std::clamp(scale, 0.1, 8.0);
    fitMode_ = keepFitMode;
    updateImageDisplay();
}

void PreviewPanel::fitCurrentImage() {
    if (!hasRasterPreview()) {
        return;
    }

    const QSize viewportSize = imageScrollArea_->viewport()->size();
    const double fitScale = gis::gui::fitScaleForSize(
        currentImage_.width(), currentImage_.height(),
        viewportSize.width() - 12, viewportSize.height() - 12);
    setScale(fitScale, true);
}

void PreviewPanel::setZoomControlsEnabled(bool enabled) {
    zoomInButton_->setEnabled(enabled);
    zoomOutButton_->setEnabled(enabled);
    fitButton_->setEnabled(enabled);
}

bool PreviewPanel::hasRasterPreview() const {
    return !currentImage_.isNull() && stackedWidget_->currentIndex() == 1;
}

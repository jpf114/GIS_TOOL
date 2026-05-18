#include "raster_preview_widget.h"
#include "style_constants.h"

#include <gis/core/gdal_wrapper.h>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>

#include <gdal_priv.h>

RasterPreviewWidget::RasterPreviewWidget(QWidget* parent)
    : QWidget(parent) {
    buildUi();
}

void RasterPreviewWidget::buildUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    auto* contentWidget = new QWidget;
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(16);

    auto* thumbCard = new QFrame;
    thumbCard->setObjectName(QStringLiteral("card"));
    auto* thumbLayout = new QVBoxLayout(thumbCard);
    thumbLayout->setContentsMargins(18, 14, 18, 14);
    thumbLayout->setSpacing(8);

    auto* thumbTitle = new QLabel(QStringLiteral("缩略图"));
    thumbTitle->setObjectName(QStringLiteral("cardTitle"));
    thumbLayout->addWidget(thumbTitle);

    thumbnailLabel_ = new QLabel;
    thumbnailLabel_->setAlignment(Qt::AlignCenter);
    thumbnailLabel_->setMinimumSize(200, 150);
    thumbnailLabel_->setStyleSheet(
        QStringLiteral("background: #1E2A36; border-radius: 6px; color: %1;")
            .arg(gis::style::Color::kTextMuted));
    thumbnailLabel_->setText(QStringLiteral("无数据"));
    thumbLayout->addWidget(thumbnailLabel_);

    contentLayout->addWidget(thumbCard);

    auto* histCard = new QFrame;
    histCard->setObjectName(QStringLiteral("card"));
    auto* histLayout = new QVBoxLayout(histCard);
    histLayout->setContentsMargins(18, 14, 18, 14);
    histLayout->setSpacing(8);

    auto* histHeader = new QHBoxLayout;
    auto* histTitle = new QLabel(QStringLiteral("直方图"));
    histTitle->setObjectName(QStringLiteral("cardTitle"));
    histHeader->addWidget(histTitle);
    histHeader->addStretch();

    bandCombo_ = new QComboBox;
    bandCombo_->setFixedWidth(120);
    bandCombo_->setObjectName(QStringLiteral("bandCombo"));
    histHeader->addWidget(bandCombo_);
    histLayout->addLayout(histHeader);

    histogramWidget_ = new QWidget;
    histogramWidget_->setFixedHeight(kHistogramHeight + 40);
    histogramWidget_->setStyleSheet(QStringLiteral("background: transparent;"));
    histLayout->addWidget(histogramWidget_);

    contentLayout->addWidget(histCard);

    auto* statsCard = new QFrame;
    statsCard->setObjectName(QStringLiteral("card"));
    auto* statsCardLayout = new QVBoxLayout(statsCard);
    statsCardLayout->setContentsMargins(18, 14, 18, 14);
    statsCardLayout->setSpacing(8);

    auto* statsTitle = new QLabel(QStringLiteral("栅格信息"));
    statsTitle->setObjectName(QStringLiteral("cardTitle"));
    statsCardLayout->addWidget(statsTitle);

    statsContainer_ = new QLabel;
    statsContainer_->setObjectName(QStringLiteral("cardDesc"));
    statsContainer_->setWordWrap(true);
    statsContainer_->setTextFormat(Qt::PlainText);
    statsContainer_->setText(QStringLiteral("无数据"));
    statsCardLayout->addWidget(statsContainer_);

    contentLayout->addWidget(statsCard);
    contentLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    mainLayout->addWidget(scrollArea);

    connect(bandCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (!rasterInfo_ || index < 0) return;
        updateHistogramForBand(index + 1);
    });
}

void RasterPreviewWidget::loadRaster(const QString& path) {
    clear();
    filePath_ = path;

    auto ds = gis::core::openRaster(path.toStdString());
    if (!ds) {
        thumbnailLabel_->setText(QStringLiteral("无法打开栅格文件"));
        return;
    }

    rasterInfo_ = std::make_unique<gis::core::RasterInfo>(
        gis::core::getRasterInfo(ds.get(), path.toStdString()));

    bandCombo_->blockSignals(true);
    bandCombo_->clear();
    for (int i = 1; i <= rasterInfo_->bandCount; ++i) {
        bandCombo_->addItem(QStringLiteral("波段 %1").arg(i));
    }
    bandCombo_->blockSignals(false);

    loadThumbnail();
    loadHistogram();
    loadStats();
}

void RasterPreviewWidget::clear() {
    filePath_.clear();
    rasterInfo_.reset();
    thumbnailLabel_->setPixmap(QPixmap());
    thumbnailLabel_->setText(QStringLiteral("无数据"));
    bandCombo_->clear();
    histogramWidget_->update();
    statsContainer_->setText(QStringLiteral("无数据"));
}

void RasterPreviewWidget::loadThumbnail() {
    if (!rasterInfo_) return;

    auto ds = gis::core::openRaster(filePath_.toStdString());
    if (!ds) return;

    int srcW = rasterInfo_->width;
    int srcH = rasterInfo_->height;
    double scale = 1.0;
    if (srcW > kThumbnailMaxSize || srcH > kThumbnailMaxSize) {
        scale = std::min(static_cast<double>(kThumbnailMaxSize) / srcW,
                         static_cast<double>(kThumbnailMaxSize) / srcH);
    }
    int dstW = std::max(1, static_cast<int>(srcW * scale));
    int dstH = std::max(1, static_cast<int>(srcH * scale));

    int bands = std::min(rasterInfo_->bandCount, 3);

    QImage image(dstW, dstH, QImage::Format_RGB32);
    image.fill(QColor(30, 42, 54));

    std::vector<float> scanline(srcW);

    for (int y = 0; y < dstH; ++y) {
        int srcY = std::min(static_cast<int>(y / scale), srcH - 1);
        for (int b = 0; b < bands; ++b) {
            auto* band = ds->GetRasterBand(b + 1);
            if (!band) continue;
            band->RasterIO(GF_Read, 0, srcY, srcW, 1,
                           scanline.data(), srcW, 1, GDT_Float32, 0, 0);

            const auto& bs = rasterInfo_->bands[static_cast<size_t>(b)];
            double range = bs.maxVal - bs.minVal;
            if (range <= 0) range = 1.0;

            for (int x = 0; x < dstW; ++x) {
                int srcX = std::min(static_cast<int>(x / scale), srcW - 1);
                double val = scanline[static_cast<size_t>(srcX)];
                if (bs.hasNoData && val == bs.noDataValue) continue;

                double norm = std::clamp((val - bs.minVal) / range, 0.0, 1.0);
                int iv = static_cast<int>(norm * 255);

                QRgb rgb = image.pixel(x, y);
                int r = qRed(rgb), g = qGreen(rgb), bl = qBlue(rgb);
                if (b == 0) r = iv;
                else if (b == 1) g = iv;
                else bl = iv;
                image.setPixel(x, y, qRgb(r, g, bl));
            }
        }
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    thumbnailLabel_->setPixmap(pixmap.scaled(
        QSize(dstW, dstH), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void RasterPreviewWidget::loadHistogram() {
    if (!rasterInfo_ || rasterInfo_->bandCount < 1) return;
    updateHistogramForBand(1);
}

void RasterPreviewWidget::updateHistogramForBand(int bandIndex) {
    if (!rasterInfo_) return;

    auto ds = gis::core::openRaster(filePath_.toStdString());
    if (!ds) return;

    auto bins = gis::core::computeHistogram(ds.get(), bandIndex, 64);
    if (bins.empty()) return;

    uint64_t maxCount = 0;
    for (const auto& bin : bins) {
        maxCount = std::max(maxCount, bin.count);
    }
    if (maxCount == 0) maxCount = 1;

    QPixmap pixmap(kHistogramWidth, kHistogramHeight);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int barWidth = kHistogramWidth / static_cast<int>(bins.size());
    int margin = 20;
    int drawHeight = kHistogramHeight - margin;

    for (size_t i = 0; i < bins.size(); ++i) {
        double ratio = static_cast<double>(bins[i].count) / static_cast<double>(maxCount);
        int barH = static_cast<int>(ratio * drawHeight);
        int x = static_cast<int>(i) * barWidth;
        int y = drawHeight - barH;

        QLinearGradient gradient(x, y, x, drawHeight);
        gradient.setColorAt(0.0, QColor("#5CB8FF"));
        gradient.setColorAt(1.0, QColor("#2E7EC9"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(x, y, barWidth - 1, barH, 1, 1);
    }

    auto* histLabel = new QLabel;
    histLabel->setPixmap(pixmap);

    auto* layout = histogramWidget_->layout();
    if (layout) {
        QLayoutItem* item;
        while ((item = layout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete layout;
    }

    auto* newLayout = new QVBoxLayout(histogramWidget_);
    newLayout->setContentsMargins(0, 0, 0, 0);
    newLayout->addWidget(histLabel);

    if (bandIndex >= 1 && bandIndex <= static_cast<int>(rasterInfo_->bands.size())) {
        const auto& bs = rasterInfo_->bands[static_cast<size_t>(bandIndex - 1)];
        auto* rangeLabel = new QLabel(
            QStringLiteral("值域: [%1, %2]  均值: %3  标准差: %4")
                .arg(bs.minVal, 0, 'g', 6)
                .arg(bs.maxVal, 0, 'g', 6)
                .arg(bs.mean, 0, 'g', 6)
                .arg(bs.stddev, 0, 'g', 6));
        rangeLabel->setObjectName(QStringLiteral("cardDesc"));
        newLayout->addWidget(rangeLabel);
    }
}

void RasterPreviewWidget::loadStats() {
    if (!rasterInfo_) return;

    const auto& info = *rasterInfo_;
    QString text;
    text += QStringLiteral("文件路径: %1\n").arg(filePath_);
    text += QStringLiteral("驱动格式: %1\n").arg(QString::fromStdString(info.driver));
    text += QStringLiteral("尺寸: %1 × %2 像素\n").arg(info.width).arg(info.height);
    text += QStringLiteral("波段数: %1\n").arg(info.bandCount);

    if (!info.crsAuth.empty()) {
        text += QStringLiteral("坐标系: %1\n").arg(QString::fromStdString(info.crsAuth));
    } else if (!info.crsWKT.empty()) {
        text += QStringLiteral("坐标系: %1\n").arg(
            QString::fromStdString(info.crsWKT).left(80));
    } else {
        text += QStringLiteral("坐标系: 未定义\n");
    }

    if (info.geoTransform[1] != 0 || info.geoTransform[5] != 0) {
        text += QStringLiteral("分辨率: %1 × %2\n")
                    .arg(std::abs(info.geoTransform[1]), 0, 'g', 6)
                    .arg(std::abs(info.geoTransform[5]), 0, 'g', 6);
    }

    double xmin = info.geoTransform[0];
    double ymax = info.geoTransform[3];
    double xmax = xmin + info.width * info.geoTransform[1];
    double ymin = ymax + info.height * info.geoTransform[5];
    text += QStringLiteral("范围: [%1, %2, %3, %4]\n")
                .arg(std::min(xmin, xmax), 0, 'g', 10)
                .arg(std::min(ymin, ymax), 0, 'g', 10)
                .arg(std::max(xmin, xmax), 0, 'g', 10)
                .arg(std::max(ymin, ymax), 0, 'g', 10);

    for (int i = 0; i < static_cast<int>(info.bands.size()); ++i) {
        const auto& bs = info.bands[static_cast<size_t>(i)];
        text += QStringLiteral("\n波段 %1: ").arg(i + 1);
        text += QString::fromStdString(bs.dataTypeName);
        if (bs.hasNoData) {
            text += QStringLiteral("  NoData=%1").arg(bs.noDataValue, 0, 'g', 6);
        }
    }

    statsContainer_->setText(text);
}

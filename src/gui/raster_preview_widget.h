#pragma once

#include <QWidget>
#include <QString>
#include <memory>

class QLabel;
class QComboBox;
class QFormLayout;

namespace gis::core {
struct RasterInfo;
struct HistogramBin;
}

class RasterPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit RasterPreviewWidget(QWidget* parent = nullptr);

    void loadRaster(const QString& path);
    void clear();

private:
    void buildUi();
    void loadThumbnail();
    void loadHistogram();
    void loadStats();
    void updateHistogramForBand(int bandIndex);

    QString filePath_;
    std::unique_ptr<gis::core::RasterInfo> rasterInfo_;

    QLabel* thumbnailLabel_ = nullptr;
    QWidget* histogramWidget_ = nullptr;
    QComboBox* bandCombo_ = nullptr;
    QFormLayout* statsLayout_ = nullptr;
    QLabel* statsContainer_ = nullptr;

    static constexpr int kThumbnailMaxSize = 512;
    static constexpr int kHistogramWidth = 480;
    static constexpr int kHistogramHeight = 160;
};

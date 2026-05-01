#include "mainwindow.h"

#include "execute_worker.h"
#include "nav_panel.h"
#include "param_widget.h"
#include "progress_dialog.h"
#include "qt_progress_reporter.h"
#include "style_constants.h"
#include "gui_data_support.h"

#include <gis/core/runtime_env.h>

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace {

struct ActionUiConfig {
    QString displayName;
    QString description;
    std::set<std::string> visibleKeys;
    std::set<std::string> requiredKeys;
};

struct ParamText {
    QString displayName;
    QString description;
};

QString genericActionDisplayName(const QString& actionKey) {
    static const std::map<QString, QString> kLabels = {
        {QStringLiteral("reproject"), QStringLiteral("\351\207\215\346\212\225\345\275\261")},
        {QStringLiteral("info"), QStringLiteral("\344\277\241\346\201\257\346\237\245\347\234\213")},
        {QStringLiteral("transform"), QStringLiteral("\345\235\220\346\240\207\350\275\254\346\215\242")},
        {QStringLiteral("assign_srs"), QStringLiteral("\350\265\213\344\272\210\345\235\220\346\240\207\347\263\273")},
        {QStringLiteral("clip"), QStringLiteral("\350\243\201\345\210\207")},
        {QStringLiteral("mosaic"), QStringLiteral("\351\225\266\345\265\214")},
        {QStringLiteral("split"), QStringLiteral("\345\210\206\345\235\227")},
        {QStringLiteral("merge_bands"), QStringLiteral("\346\263\242\346\256\265\345\220\210\345\271\266")},
        {QStringLiteral("detect"), QStringLiteral("\347\211\271\345\276\201\346\243\200\346\265\213")},
        {QStringLiteral("match"), QStringLiteral("\347\211\271\345\276\201\345\214\271\351\205\215")},
        {QStringLiteral("register"), QStringLiteral("\345\275\261\345\203\217\351\205\215\345\207\206")},
        {QStringLiteral("change"), QStringLiteral("\345\217\230\345\214\226\346\243\200\346\265\213")},
        {QStringLiteral("ecc_register"), QStringLiteral("ECC \351\205\215\345\207\206")},
        {QStringLiteral("corner"), QStringLiteral("\350\247\222\347\202\271\346\243\200\346\265\213")},
        {QStringLiteral("stitch"), QStringLiteral("\345\233\276\345\203\217\346\213\274\346\216\245")},
        {QStringLiteral("threshold"), QStringLiteral("\351\230\210\345\200\274\345\210\206\345\211\262")},
        {QStringLiteral("filter"), QStringLiteral("\346\273\244\346\263\242")},
        {QStringLiteral("enhance"), QStringLiteral("\345\242\236\345\274\272")},
        {QStringLiteral("band_math"), QStringLiteral("\346\263\242\346\256\265\350\277\220\347\256\227")},
        {QStringLiteral("stats"), QStringLiteral("\347\273\237\350\256\241\344\277\241\346\201\257")},
        {QStringLiteral("edge"), QStringLiteral("\350\276\271\347\274\230\346\243\200\346\265\213")},
        {QStringLiteral("contour"), QStringLiteral("\350\275\256\345\273\223\346\217\220\345\217\226")},
        {QStringLiteral("template_match"), QStringLiteral("\346\250\241\346\235\277\345\214\271\351\205\215")},
        {QStringLiteral("pansharpen"), QStringLiteral("\345\205\250\350\211\262\351\224\220\345\214\226")},
        {QStringLiteral("hough"), QStringLiteral("\351\234\215\345\244\253\345\217\230\346\215\242")},
        {QStringLiteral("watershed"), QStringLiteral("\345\210\206\346\260\264\345\262\255")},
        {QStringLiteral("kmeans"), QStringLiteral("K-Means")},
        {QStringLiteral("overviews"), QStringLiteral("\351\207\221\345\255\227\345\241\224")},
        {QStringLiteral("nodata"), QStringLiteral("NoData \350\256\276\347\275\256")},
        {QStringLiteral("histogram"), QStringLiteral("\347\233\264\346\226\271\345\233\276")},
        {QStringLiteral("colormap"), QStringLiteral("\344\274\252\345\275\251\350\211\262")},
        {QStringLiteral("ndvi"), QStringLiteral("NDVI")},
        {QStringLiteral("evi"), QStringLiteral("EVI")},
        {QStringLiteral("savi"), QStringLiteral("SAVI")},
        {QStringLiteral("gndvi"), QStringLiteral("GNDVI")},
        {QStringLiteral("ndwi"), QStringLiteral("NDWI")},
        {QStringLiteral("mndwi"), QStringLiteral("MNDWI")},
        {QStringLiteral("ndbi"), QStringLiteral("NDBI")},
        {QStringLiteral("custom_index"), QStringLiteral("自定义指数")},
        {QStringLiteral("buffer"), QStringLiteral("\347\274\223\345\206\262\345\214\272")},
        {QStringLiteral("rasterize"), QStringLiteral("\346\240\205\346\240\274\345\214\226")},
        {QStringLiteral("polygonize"), QStringLiteral("\351\235\242\347\237\242\351\207\217\345\214\226")},
        {QStringLiteral("convert"), QStringLiteral("\346\240\274\345\274\217\350\275\254\346\215\242")},
        {QStringLiteral("union"), QStringLiteral("\345\271\266\351\233\206")},
        {QStringLiteral("difference"), QStringLiteral("\345\267\256\351\233\206")},
        {QStringLiteral("dissolve"), QStringLiteral("\350\236\215\345\220\210")},
    };

    const auto it = kLabels.find(actionKey);
    if (it != kLabels.end()) {
        return it->second;
    }
    return actionKey;
}

QString actionIconText(const QString& actionKey) {
    return actionKey.isEmpty() ? QStringLiteral("default") : actionKey;
}

QPixmap badgeIconPixmap(const QString& text, const QColor& bg, const QColor& fg, int size = 38) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawRoundedRect(QRectF(0.5, 0.5, size - 1.0, size - 1.0), 8, 8);

    QPen pen(fg);
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    auto drawGrid = [&]() {
        painter.drawRect(QRectF(10, 10, 18, 18));
        painter.drawLine(QPointF(19, 10), QPointF(19, 28));
        painter.drawLine(QPointF(10, 19), QPointF(28, 19));
    };
    auto drawBars = [&]() {
        painter.drawLine(QPointF(14, 26), QPointF(14, 18));
        painter.drawLine(QPointF(19, 26), QPointF(19, 14));
        painter.drawLine(QPointF(24, 26), QPointF(24, 10));
    };
    auto drawScissors = [&]() {
        painter.drawLine(QPointF(12, 12), QPointF(26, 26));
        painter.drawLine(QPointF(26, 12), QPointF(12, 26));
        painter.drawEllipse(QRectF(10.4, 10.4, 4.2, 4.2));
        painter.drawEllipse(QRectF(23.4, 10.4, 4.2, 4.2));
    };
    auto drawNodes = [&]() {
        painter.drawEllipse(QRectF(11.2, 11.2, 4.2, 4.2));
        painter.drawEllipse(QRectF(22.2, 12.2, 4.2, 4.2));
        painter.drawEllipse(QRectF(17.2, 22.0, 4.2, 4.2));
        painter.drawLine(QPointF(15.0, 14.0), QPointF(22.0, 14.6));
        painter.drawLine(QPointF(23.0, 16.0), QPointF(20.0, 22.2));
        painter.drawLine(QPointF(17.9, 22.0), QPointF(14.1, 15.8));
    };

    if (text == QStringLiteral("threshold")) {
        painter.drawLine(QPointF(10, 26), QPointF(15, 21));
        painter.drawLine(QPointF(15, 21), QPointF(19, 24));
        painter.drawLine(QPointF(19, 24), QPointF(28, 13));
        painter.drawRect(QRectF(9.5, 9.5, 18, 18));
    } else if (text == QStringLiteral("filter")) {
        painter.drawEllipse(QRectF(10, 10, 18, 18));
        painter.drawLine(QPointF(10, 19), QPointF(28, 19));
    } else if (text == QStringLiteral("reproject")) {
        painter.drawEllipse(QRectF(10, 10, 18, 18));
        painter.drawLine(QPointF(19, 10), QPointF(19, 28));
        painter.drawLine(QPointF(10, 19), QPointF(28, 19));
        painter.drawLine(QPointF(13, 13), QPointF(25, 25));
    } else if (text == QStringLiteral("transform")) {
        painter.drawEllipse(QRectF(10, 10, 18, 18));
        painter.drawLine(QPointF(19, 10), QPointF(19, 28));
        painter.drawLine(QPointF(10, 19), QPointF(28, 19));
        painter.drawEllipse(QRectF(16, 16, 6, 6));
    } else if (text == QStringLiteral("assign_srs")) {
        painter.drawRoundedRect(QRectF(10, 12, 18, 14), 3, 3);
        painter.drawLine(QPointF(14, 16), QPointF(24, 16));
        painter.drawLine(QPointF(14, 20), QPointF(22, 20));
        painter.drawLine(QPointF(19, 9), QPointF(19, 12));
    } else if (text == QStringLiteral("projection")) {
        painter.drawEllipse(QRectF(10, 10, 18, 18));
        painter.drawLine(QPointF(19, 10), QPointF(19, 28));
        painter.drawLine(QPointF(10, 19), QPointF(28, 19));
    } else if (text == QStringLiteral("processing") || text == QStringLiteral("default")) {
        painter.drawRect(QRectF(10, 10, 18, 18));
        painter.drawLine(QPointF(12, 24), QPointF(17, 18));
        painter.drawLine(QPointF(17, 18), QPointF(21, 21));
        painter.drawLine(QPointF(21, 21), QPointF(26, 14));
    } else if (text == QStringLiteral("cutting") || text == QStringLiteral("clip")) {
        drawScissors();
    } else if (text == QStringLiteral("mosaic")) {
        drawGrid();
        painter.drawLine(QPointF(10, 10), QPointF(28, 28));
    } else if (text == QStringLiteral("split")) {
        drawGrid();
        painter.drawLine(QPointF(19, 10), QPointF(19, 28));
        painter.drawLine(QPointF(10, 19), QPointF(28, 19));
    } else if (text == QStringLiteral("merge_bands")) {
        painter.drawRect(QRectF(11, 12, 14, 12));
        painter.drawRect(QRectF(14, 9, 14, 12));
        painter.drawRect(QRectF(17, 6, 10, 10));
    } else if (text == QStringLiteral("matching")) {
        painter.drawEllipse(QRectF(10, 10, 18, 18));
        painter.drawLine(QPointF(19, 11.5), QPointF(19, 26.5));
        painter.drawLine(QPointF(11.5, 19), QPointF(26.5, 19));
        painter.drawEllipse(QRectF(16.2, 16.2, 5.6, 5.6));
    } else if (text == QStringLiteral("classification")) {
        drawGrid();
        painter.drawPoint(QPointF(14.0, 14.0));
        painter.drawPoint(QPointF(24.0, 14.0));
        painter.drawPoint(QPointF(14.0, 24.0));
    } else if (text == QStringLiteral("spindex")) {
        painter.drawEllipse(QRectF(12, 10, 12, 18));
        painter.drawLine(QPointF(18, 12), QPointF(18, 26));
        painter.drawLine(QPointF(18, 18), QPointF(24, 12));
    } else if (text == QStringLiteral("detect")) {
        painter.drawEllipse(QRectF(11, 11, 16, 16));
        painter.drawLine(QPointF(19, 8.5), QPointF(19, 13));
        painter.drawLine(QPointF(19, 25), QPointF(19, 29.5));
        painter.drawLine(QPointF(8.5, 19), QPointF(13, 19));
        painter.drawLine(QPointF(25, 19), QPointF(29.5, 19));
    } else if (text == QStringLiteral("match")) {
        painter.drawEllipse(QRectF(10, 10, 10, 10));
        painter.drawEllipse(QRectF(18, 18, 10, 10));
        painter.drawLine(QPointF(18, 18), QPointF(20, 20));
    } else if (text == QStringLiteral("register") || text == QStringLiteral("ecc_register")) {
        painter.drawRect(QRectF(10, 12, 10, 10));
        painter.drawRect(QRectF(18, 16, 10, 10));
        painter.drawLine(QPointF(16, 8), QPointF(22, 8));
        painter.drawLine(QPointF(22, 8), QPointF(20, 6));
        painter.drawLine(QPointF(22, 8), QPointF(20, 10));
    } else if (text == QStringLiteral("change")) {
        painter.drawRect(QRectF(10, 12, 8, 8));
        painter.drawRect(QRectF(20, 18, 8, 8));
        painter.drawLine(QPointF(16, 16), QPointF(22, 22));
    } else if (text == QStringLiteral("corner")) {
        painter.drawLine(QPointF(11, 11), QPointF(11, 27));
        painter.drawLine(QPointF(11, 27), QPointF(27, 27));
        painter.drawEllipse(QRectF(17, 17, 4, 4));
    } else if (text == QStringLiteral("stitch")) {
        painter.drawRoundedRect(QRectF(10, 13, 9, 11), 2, 2);
        painter.drawRoundedRect(QRectF(19, 13, 9, 11), 2, 2);
        painter.drawLine(QPointF(19, 18.5), QPointF(19, 18.5));
    } else if (text == QStringLiteral("feature_stats")) {
        drawGrid();
        painter.drawPoint(QPointF(14.0, 14.0));
        painter.drawPoint(QPointF(24.0, 14.0));
        painter.drawPoint(QPointF(14.0, 24.0));
    } else if (text == QStringLiteral("info") || text == QStringLiteral("stats")) {
        drawBars();
        painter.drawEllipse(QRectF(12.4, 8.5, 3.2, 3.2));
        painter.drawEllipse(QRectF(17.4, 12.5, 3.2, 3.2));
        painter.drawEllipse(QRectF(22.4, 6.5, 3.2, 3.2));
    } else if (text == QStringLiteral("enhance")) {
        painter.drawLine(QPointF(19, 10), QPointF(19, 28));
        painter.drawLine(QPointF(10, 19), QPointF(28, 19));
        painter.drawLine(QPointF(12.5, 12.5), QPointF(25.5, 25.5));
    } else if (text == QStringLiteral("band_math")) {
        painter.drawLine(QPointF(19, 10), QPointF(19, 28));
        painter.drawLine(QPointF(10, 19), QPointF(28, 19));
        painter.drawLine(QPointF(12, 12), QPointF(15, 15));
        painter.drawLine(QPointF(15, 12), QPointF(12, 15));
    } else if (text == QStringLiteral("edge")) {
        painter.drawPolyline(QPolygonF() << QPointF(11, 24) << QPointF(16, 14) << QPointF(20, 22) << QPointF(27, 11));
    } else if (text == QStringLiteral("contour")) {
        painter.drawEllipse(QRectF(10, 10, 18, 18));
        painter.drawEllipse(QRectF(14, 14, 10, 10));
    } else if (text == QStringLiteral("template_match")) {
        painter.drawRect(QRectF(10, 10, 18, 18));
        painter.drawRect(QRectF(15, 15, 8, 8));
    } else if (text == QStringLiteral("pansharpen")) {
        painter.drawEllipse(QRectF(11, 11, 8, 8));
        painter.drawLine(QPointF(23, 12), QPointF(23, 27));
        painter.drawLine(QPointF(20, 19.5), QPointF(27, 19.5));
    } else if (text == QStringLiteral("hough")) {
        painter.drawLine(QPointF(11, 26), QPointF(27, 10));
        painter.drawEllipse(QRectF(18, 14, 8, 8));
    } else if (text == QStringLiteral("watershed")) {
        painter.drawEllipse(QRectF(14, 11, 10, 14));
        painter.drawLine(QPointF(19, 25), QPointF(19, 28));
    } else if (text == QStringLiteral("kmeans")) {
        drawNodes();
    } else if (text == QStringLiteral("utility")) {
        drawBars();
    } else if (text == QStringLiteral("vector")) {
        drawNodes();
    } else if (text == QStringLiteral("overviews")) {
        painter.drawRect(QRectF(10, 10, 18, 18));
        painter.drawRect(QRectF(13, 13, 12, 12));
        painter.drawRect(QRectF(16, 16, 6, 6));
    } else if (text == QStringLiteral("nodata")) {
        painter.drawEllipse(QRectF(11, 11, 16, 16));
        painter.drawLine(QPointF(12, 26), QPointF(26, 12));
    } else if (text == QStringLiteral("histogram")) {
        drawBars();
    } else if (text == QStringLiteral("colormap")) {
        painter.drawRoundedRect(QRectF(10, 12, 18, 14), 4, 4);
        painter.drawLine(QPointF(15, 16), QPointF(15, 22));
        painter.drawLine(QPointF(19, 14), QPointF(19, 24));
        painter.drawLine(QPointF(23, 16), QPointF(23, 22));
    } else if (text == QStringLiteral("ndvi")) {
        painter.drawEllipse(QRectF(12, 10, 12, 18));
        painter.drawLine(QPointF(18, 12), QPointF(18, 26));
        painter.drawLine(QPointF(18, 18), QPointF(24, 12));
    } else if (text == QStringLiteral("buffer")) {
        painter.drawEllipse(QRectF(14, 14, 10, 10));
        painter.drawEllipse(QRectF(10, 10, 18, 18));
    } else if (text == QStringLiteral("rasterize")) {
        drawGrid();
    } else if (text == QStringLiteral("polygonize")) {
        painter.drawPolygon(QPolygonF() << QPointF(12, 14) << QPointF(18, 10) << QPointF(26, 15) << QPointF(23, 24) << QPointF(14, 26));
    } else if (text == QStringLiteral("convert")) {
        painter.drawLine(QPointF(11, 14), QPointF(26, 14));
        painter.drawLine(QPointF(21, 10), QPointF(26, 14));
        painter.drawLine(QPointF(21, 18), QPointF(26, 14));
        painter.drawLine(QPointF(27, 24), QPointF(12, 24));
        painter.drawLine(QPointF(17, 20), QPointF(12, 24));
        painter.drawLine(QPointF(17, 28), QPointF(12, 24));
    } else if (text == QStringLiteral("union")) {
        painter.drawEllipse(QRectF(10, 12, 10, 10));
        painter.drawEllipse(QRectF(18, 12, 10, 10));
    } else if (text == QStringLiteral("difference")) {
        painter.drawEllipse(QRectF(10, 12, 12, 12));
        painter.drawLine(QPointF(24, 18), QPointF(28, 18));
    } else if (text == QStringLiteral("dissolve")) {
        painter.drawEllipse(QRectF(10, 12, 8, 8));
        painter.drawEllipse(QRectF(18, 12, 8, 8));
        painter.drawEllipse(QRectF(14, 18, 8, 8));
    } else {
        painter.drawEllipse(QRectF(11, 11, 16, 16));
        painter.drawPoint(QPointF(19, 19));
    }
    return pixmap;
}

QIcon executeIcon() {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);

    QPolygonF triangle;
    triangle << QPointF(4.0, 2.5) << QPointF(13.0, 8.0) << QPointF(4.0, 13.5);
    painter.drawPolygon(triangle);
    return QIcon(pixmap);
}

const std::map<std::string, ParamText>& commonParamTextStorage() {
    static const std::map<std::string, ParamText> kTexts = {
        {"input", {QStringLiteral("输入文件"), QStringLiteral("输入数据路径；多文件场景请使用英文逗号分隔。")}},
        {"output", {QStringLiteral("输出文件"), QStringLiteral("输出结果路径；建议按功能选择正确后缀。")}},
        {"reference", {QStringLiteral("参考文件"), QStringLiteral("参考数据路径，常用于匹配、配准或变化检测。")}},
        {"dst_srs", {QStringLiteral("目标坐标系"), QStringLiteral("目标坐标参考系，例如 EPSG:3857。")}},
        {"src_srs", {QStringLiteral("源坐标系"), QStringLiteral("源坐标参考系，留空时尽量使用数据自带坐标系。")}},
        {"srs", {QStringLiteral("坐标系"), QStringLiteral("要写入数据的坐标参考系。")}},
        {"resample", {QStringLiteral("重采样方式"), QStringLiteral("输出计算或重投影时采用的重采样算法。")}},
        {"x", {QStringLiteral("X 坐标"), QStringLiteral("待转换点的 X 坐标。")}},
        {"y", {QStringLiteral("Y 坐标"), QStringLiteral("待转换点的 Y 坐标。")}},
        {"extent", {QStringLiteral("空间范围"), QStringLiteral("矩形范围 xmin, ymin, xmax, ymax。")}},
        {"cutline", {QStringLiteral("裁切矢量"), QStringLiteral("用于裁切影像的矢量文件。")}},
        {"tile_size", {QStringLiteral("分块大小"), QStringLiteral("切块时每块的像素尺寸。")}},
        {"overlap", {QStringLiteral("重叠像素"), QStringLiteral("相邻分块之间的重叠像素数。")}},
        {"bands", {QStringLiteral("波段列表"), QStringLiteral("按功能填写逗号分隔列表，例如 1,1,1 或 band1.tif,band2.tif。")}},
        {"layer", {QStringLiteral("图层名"), QStringLiteral("要处理的图层名称，留空时通常使用第一个图层。")}},
        {"where", {QStringLiteral("属性过滤"), QStringLiteral("SQL WHERE 条件表达式。")}},
        {"distance", {QStringLiteral("距离"), QStringLiteral("缓冲区或相关分析的距离值。")}},
        {"clip_vector", {QStringLiteral("裁切矢量"), QStringLiteral("用于裁切当前输入矢量的叠加矢量文件。")}},
        {"resolution", {QStringLiteral("分辨率"), QStringLiteral("输出栅格的像元大小。")}},
        {"attribute", {QStringLiteral("属性字段"), QStringLiteral("用于栅格化写值的字段名。")}},
        {"band", {QStringLiteral("波段号"), QStringLiteral("要处理的波段序号，从 1 开始。")}},
        {"format", {QStringLiteral("输出格式"), QStringLiteral("输出数据格式。")}},
        {"overlay_vector", {QStringLiteral("叠加矢量"), QStringLiteral("用于并集或差集分析的第二个矢量文件。")}},
        {"dissolve_field", {QStringLiteral("融合字段"), QStringLiteral("按该字段值对相邻要素进行融合。")}},
        {"method", {QStringLiteral("方法"), QStringLiteral("当前算法使用的主要处理方法。")}},
        {"match_method", {QStringLiteral("匹配方法"), QStringLiteral("特征或模板匹配所采用的方法。")}},
        {"max_points", {QStringLiteral("最大特征点数"), QStringLiteral("允许检测的最大特征点数量。")}},
        {"ratio_test", {QStringLiteral("比率阈值"), QStringLiteral("Lowe 比率测试阈值。")}},
        {"transform", {QStringLiteral("变换模型"), QStringLiteral("配准时使用的几何变换模型。")}},
        {"change_method", {QStringLiteral("变化方法"), QStringLiteral("变化检测的计算方式。")}},
        {"threshold", {QStringLiteral("阈值"), QStringLiteral("变化检测或相关处理的阈值。")}},
        {"ecc_motion", {QStringLiteral("ECC 运动模型"), QStringLiteral("ECC 配准采用的运动模型。")}},
        {"ecc_iterations", {QStringLiteral("ECC 迭代次数"), QStringLiteral("ECC 配准最大迭代次数。")}},
        {"ecc_epsilon", {QStringLiteral("ECC 收敛阈值"), QStringLiteral("ECC 配准终止的收敛阈值。")}},
        {"corner_method", {QStringLiteral("角点方法"), QStringLiteral("角点检测采用的算法。")}},
        {"max_corners", {QStringLiteral("最大角点数"), QStringLiteral("最多输出的角点数量。")}},
        {"quality_level", {QStringLiteral("质量阈值"), QStringLiteral("角点质量控制阈值。")}},
        {"min_distance", {QStringLiteral("最小间距"), QStringLiteral("角点之间的最小距离。")}},
        {"stitch_confidence", {QStringLiteral("拼接置信度"), QStringLiteral("图像拼接的置信度阈值。")}},
        {"threshold_value", {QStringLiteral("阈值值"), QStringLiteral("阈值分割时使用的数值阈值。")}},
        {"max_value", {QStringLiteral("最大值"), QStringLiteral("阈值分割输出的最大像素值。")}},
        {"filter_type", {QStringLiteral("滤波类型"), QStringLiteral("空间滤波算法类型。")}},
        {"kernel_size", {QStringLiteral("核大小"), QStringLiteral("滤波核或结构元素大小。")}},
        {"sigma", {QStringLiteral("Sigma"), QStringLiteral("高斯等滤波的 sigma 参数。")}},
        {"enhance_type", {QStringLiteral("增强类型"), QStringLiteral("影像增强算法类型。")}},
        {"clip_limit", {QStringLiteral("裁剪限制"), QStringLiteral("CLAHE 的裁剪限制参数。")}},
        {"gamma", {QStringLiteral("Gamma"), QStringLiteral("Gamma 校正参数。")}},
        {"expression", {QStringLiteral("表达式"), QStringLiteral("波段运算表达式，例如 B1+B2。")}},
        {"edge_method", {QStringLiteral("边缘方法"), QStringLiteral("边缘检测算法类型。")}},
        {"low_threshold", {QStringLiteral("低阈值"), QStringLiteral("边缘检测的低阈值。")}},
        {"high_threshold", {QStringLiteral("高阈值"), QStringLiteral("边缘检测的高阈值。")}},
        {"sobel_dx", {QStringLiteral("Sobel dx"), QStringLiteral("Sobel 的 x 方向导数阶数。")}},
        {"sobel_dy", {QStringLiteral("Sobel dy"), QStringLiteral("Sobel 的 y 方向导数阶数。")}},
        {"min_area", {QStringLiteral("最小面积"), QStringLiteral("轮廓筛选的最小面积。")}},
        {"template_file", {QStringLiteral("模板文件"), QStringLiteral("用于模板匹配的模板影像，建议使用栅格文件。")}},
        {"pan_file", {QStringLiteral("全色影像"), QStringLiteral("全色锐化所需的高分辨率全色影像。")}},
        {"pan_method", {QStringLiteral("融合方法"), QStringLiteral("全色锐化采用的融合方法。")}},
        {"hough_type", {QStringLiteral("霍夫类型"), QStringLiteral("霍夫检测类型，例如直线或圆。")}},
        {"hough_threshold", {QStringLiteral("霍夫阈值"), QStringLiteral("霍夫变换累加器阈值。")}},
        {"min_line_length", {QStringLiteral("最小线长"), QStringLiteral("霍夫直线检测的最小线段长度。")}},
        {"max_line_gap", {QStringLiteral("最大线间隙"), QStringLiteral("霍夫直线检测允许的最大线段间隙。")}},
        {"min_radius", {QStringLiteral("最小半径"), QStringLiteral("霍夫圆检测的最小半径。")}},
        {"max_radius", {QStringLiteral("最大半径"), QStringLiteral("霍夫圆检测的最大半径。")}},
        {"circle_param2", {QStringLiteral("圆检测阈值"), QStringLiteral("霍夫圆检测的累加器阈值。")}},
        {"marker_input", {QStringLiteral("标记输入"), QStringLiteral("分水岭分割的外部标记输入。")}},
        {"k", {QStringLiteral("聚类数"), QStringLiteral("K-Means 的聚类类别数。")}},
        {"max_iter", {QStringLiteral("最大迭代"), QStringLiteral("K-Means 的最大迭代次数。")}},
        {"epsilon_kmeans", {QStringLiteral("收敛阈值"), QStringLiteral("K-Means 的终止收敛阈值。")}},
        {"levels", {QStringLiteral("金字塔层级"), QStringLiteral("金字塔缩放层级，多个值用空格分隔。")}},
        {"nodata_value", {QStringLiteral("NoData 值"), QStringLiteral("要写入的 NoData 数值。")}},
        {"bins", {QStringLiteral("分箱数"), QStringLiteral("直方图的分箱数量。")}},
        {"cmap", {QStringLiteral("颜色映射"), QStringLiteral("伪彩色映射方案。")}},
        {"red_band", {QStringLiteral("红光波段"), QStringLiteral("计算 NDVI 的红光波段序号。")}},
        {"nir_band", {QStringLiteral("近红外波段"), QStringLiteral("计算 NDVI 的近红外波段序号。")}},
        {"blue_band", {QStringLiteral("蓝光波段"), QStringLiteral("计算 EVI 使用的蓝光波段序号。")}},
        {"green_band", {QStringLiteral("绿光波段"), QStringLiteral("计算 GNDVI、NDWI、MNDWI 使用的绿光波段序号。")}},
        {"swir1_band", {QStringLiteral("短波红外1波段"), QStringLiteral("计算 MNDWI、NDBI 使用的短波红外1波段序号。")}},
        {"l_value", {QStringLiteral("L 参数"), QStringLiteral("SAVI 或 EVI 使用的 L 参数。")}},
        {"g_value", {QStringLiteral("G 参数"), QStringLiteral("EVI 使用的增益参数 G。")}},
        {"c1", {QStringLiteral("C1 参数"), QStringLiteral("EVI 使用的 C1 参数。")}},
        {"c2", {QStringLiteral("C2 参数"), QStringLiteral("EVI 使用的 C2 参数。")}},
        {"vector", {QStringLiteral("输入面矢量"), QStringLiteral("参与统计的面矢量文件路径。")}},
        {"feature_id_field", {QStringLiteral("要素 ID 字段"), QStringLiteral("可选，用于标识每个面要素的唯一字段名。")}},
        {"feature_name_field", {QStringLiteral("要素名称字段"), QStringLiteral("可选，用于读取面要素名称的字段名。")}},
        {"class_map", {QStringLiteral("分类映射"), QStringLiteral("分类值到分类名称的 JSON 映射文件，例如 {\"1\":\"耕地\",\"2\":\"林地\"}。")}},
        {"rasters", {QStringLiteral("分类栅格列表"), QStringLiteral("多个分类栅格路径，使用英文逗号分隔，例如 a.tif,b.tif。")}},
        {"nodatas", {QStringLiteral("NoData 列表"), QStringLiteral("与分类栅格一一对应的 NoData 列表，使用英文逗号分隔，例如 0,0,255。")}},
        {"target_epsg", {QStringLiteral("目标 EPSG"), QStringLiteral("可选，显式指定统计时使用的目标投影坐标系。")}},
        {"vector_output", {QStringLiteral("分类面输出"), QStringLiteral("可选，输出分类面结果，当前仅支持 .gpkg。")}},
        {"raster_output", {QStringLiteral("分类栅格输出"), QStringLiteral("可选，输出分类栅格结果，当前仅支持 .tif 或 .tiff。")}},
    };
    return kTexts;
}

const ParamText* findActionSpecificParamText(const std::string& pluginName,
                                             const std::string& actionKey,
                                             const std::string& paramKey) {
    static const std::map<std::string, std::map<std::string, std::map<std::string, ParamText>>> kTexts = {
        {"cutting", {
            {"split", {
                {"output", {QStringLiteral("输出目录"), QStringLiteral("分块输出目录，图块会自动命名为 tile_x_y.tif。")}},
            }},
            {"merge_bands", {
                {"input", {QStringLiteral("输入文件"), QStringLiteral("可填写一个或多个单波段栅格路径，使用英文逗号分隔。")}},
                {"bands", {QStringLiteral("波段列表"), QStringLiteral("补充更多单波段栅格路径，使用英文逗号分隔。")}},
            }},
        }},
        {"projection", {
            {"reproject", {
                {"input", {QStringLiteral("输入文件"), QStringLiteral("支持栅格或矢量数据，输出格式由输出后缀决定。")}},
            }},
            {"transform", {
                {"src_srs", {QStringLiteral("源坐标系"), QStringLiteral("源坐标系，留空时默认按 EPSG:4326 解释输入坐标。")}},
            }},
        }},
        {"processing", {
            {"filter", {
                {"kernel_size", {QStringLiteral("核大小"), QStringLiteral("滤波核大小，建议填写大于等于 3 的奇数。")}},
            }},
            {"template_match", {
                {"template_file", {QStringLiteral("模板文件"), QStringLiteral("模板影像路径，尺寸需小于等于输入影像。")}},
            }},
            {"watershed", {
                {"marker_input", {QStringLiteral("标记输入"), QStringLiteral("可选外部标记栅格，0 表示背景，1/2/3 表示不同种子区域。")}},
            }},
        }},
        {"utility", {
            {"nodata", {
                {"band", {QStringLiteral("波段序号"), QStringLiteral("填写 0 表示对全部波段设置 NoData；填写 1、2、3... 表示单个波段。")}},
            }},
        }},
        {"classification", {
            {"feature_stats", {
                {"output", {QStringLiteral("统计输出"), QStringLiteral("统计结果输出路径，仅支持 .json 或 .csv。")}},
                {"class_map", {QStringLiteral("分类映射"), QStringLiteral("JSON 文件，例如 {\"1\":\"耕地\",\"2\":\"林地\"}。")}},
                {"rasters", {QStringLiteral("分类栅格列表"), QStringLiteral("多个分类栅格路径，使用英文逗号分隔，顺序即优先级。")}},
                {"bands", {QStringLiteral("波段列表"), QStringLiteral("与分类栅格一一对应，使用英文逗号分隔，默认全部为 1。")}},
                {"nodatas", {QStringLiteral("NoData 列表"), QStringLiteral("与分类栅格一一对应，使用英文逗号分隔，默认全部为 0。")}},
            }},
        }},
        {"vector", {
            {"convert", {
                {"output", {QStringLiteral("输出文件"), QStringLiteral("输出路径应与输出格式一致，例如 .geojson、.gpkg、.shp。")}},
            }},
        }},
    };

    const auto pluginIt = kTexts.find(pluginName);
    if (pluginIt == kTexts.end()) {
        return nullptr;
    }
    const auto actionIt = pluginIt->second.find(actionKey);
    if (actionIt == pluginIt->second.end()) {
        return nullptr;
    }
    const auto paramIt = actionIt->second.find(paramKey);
    if (paramIt == actionIt->second.end()) {
        return nullptr;
    }
    return &paramIt->second;
}
const std::map<std::string, std::map<std::string, ActionUiConfig>>& actionUiConfigStorage() {
    static const std::map<std::string, std::map<std::string, ActionUiConfig>> kConfigs = {
        {"projection", {
            {"reproject", {QStringLiteral("重投影"), QStringLiteral("将栅格或矢量数据重投影到目标坐标系。"), {"input", "output", "dst_srs", "src_srs", "resample"}, {"input", "output", "dst_srs"}}},
            {"info", {QStringLiteral("栅格坐标信息"), QStringLiteral("查看栅格坐标参考、范围和分辨率信息。"), {"input"}, {"input"}}},
            {"transform", {QStringLiteral("坐标转换"), QStringLiteral("将单个坐标点从源坐标系转换到目标坐标系。"), {"src_srs", "dst_srs", "x", "y"}, {"dst_srs"}}},
            {"assign_srs", {QStringLiteral("赋予栅格坐标系"), QStringLiteral("为没有坐标参考的栅格数据直接写入坐标系定义。"), {"input", "srs"}, {"input", "srs"}}},
        }},
        {"cutting", {
            {"clip", {QStringLiteral("裁切"), QStringLiteral("按范围或裁切矢量裁切影像，范围和裁切矢量至少填写一个。"), {"input", "output", "extent", "cutline"}, {"input", "output"}}},
            {"mosaic", {QStringLiteral("镶嵌"), QStringLiteral("将多幅影像拼接为一幅，可选统一目标坐标系和重采样方式。"), {"input", "output", "dst_srs", "resample"}, {"input", "output"}}},
            {"split", {QStringLiteral("分块"), QStringLiteral("按固定块大小切分影像，可设置重叠像素。"), {"input", "output", "tile_size", "overlap"}, {"input", "output"}}},
            {"merge_bands", {QStringLiteral("波段合并"), QStringLiteral("将多个单波段文件按顺序合并为多波段栅格。"), {"input", "bands", "output"}, {"output"}}},
        }},
        {"matching", {
            {"detect", {QStringLiteral("特征检测"), QStringLiteral("提取关键点并可导出为 JSON。"), {"input", "output", "method", "max_points", "band"}, {"input"}}},
            {"match", {QStringLiteral("特征匹配"), QStringLiteral("比较参考影像和待匹配影像，输出匹配点统计。"), {"reference", "input", "output", "method", "match_method", "max_points", "ratio_test", "band"}, {"reference", "input"}}},
            {"register", {QStringLiteral("影像配准"), QStringLiteral("根据特征点匹配结果生成配准后的输出影像。"), {"reference", "input", "output", "method", "match_method", "transform", "resample", "max_points", "ratio_test", "band"}, {"reference", "input", "output"}}},
            {"change", {QStringLiteral("变化检测"), QStringLiteral("对前后两景影像执行变化检测并输出变化图。"), {"reference", "input", "output", "change_method", "threshold", "band"}, {"reference", "input", "output"}}},
            {"ecc_register", {QStringLiteral("ECC 配准"), QStringLiteral("使用 ECC 优化进行影像精配准。"), {"reference", "input", "output", "ecc_motion", "ecc_iterations", "ecc_epsilon", "resample", "band"}, {"reference", "input", "output"}}},
            {"corner", {QStringLiteral("角点检测"), QStringLiteral("提取 Harris 或 Shi-Tomasi 角点。"), {"input", "output", "corner_method", "max_corners", "quality_level", "min_distance", "band"}, {"input"}}},
            {"stitch", {QStringLiteral("图像拼接"), QStringLiteral("将多幅输入影像拼接为一张全景结果。"), {"input", "output", "stitch_confidence"}, {"input", "output"}}},
        }},
        {"processing", {
            {"threshold", {QStringLiteral("阈值分割"), QStringLiteral("按指定阈值方法生成分割结果。"), {"input", "output", "band", "method", "threshold_value", "max_value"}, {"input", "output"}}},
            {"filter", {QStringLiteral("空间滤波"), QStringLiteral("对影像执行平滑、形态学等滤波操作。"), {"input", "output", "band", "filter_type", "kernel_size", "sigma"}, {"input", "output"}}},
            {"enhance", {QStringLiteral("影像增强"), QStringLiteral("执行均衡化、CLAHE、归一化、Gamma 等增强。"), {"input", "output", "band", "enhance_type", "clip_limit", "gamma"}, {"input", "output"}}},
            {"band_math", {QStringLiteral("波段运算"), QStringLiteral("按表达式对多波段影像进行计算。"), {"input", "output", "expression"}, {"input", "output", "expression"}}},
            {"stats", {QStringLiteral("统计信息"), QStringLiteral("统计指定波段的基础数值信息。"), {"input", "band"}, {"input"}}},
            {"edge", {QStringLiteral("边缘检测"), QStringLiteral("执行 Canny、Sobel、Laplacian、Scharr 等边缘检测。"), {"input", "output", "band", "edge_method", "low_threshold", "high_threshold", "sobel_dx", "sobel_dy"}, {"input", "output"}}},
            {"contour", {QStringLiteral("轮廓提取"), QStringLiteral("根据轮廓面积阈值提取目标轮廓。"), {"input", "output", "band", "min_area"}, {"input", "output"}}},
            {"template_match", {QStringLiteral("模板匹配"), QStringLiteral("使用模板影像在输入影像中查找匹配目标。"), {"input", "output", "band", "template_file", "match_method"}, {"input", "output", "template_file"}}},
            {"pansharpen", {QStringLiteral("全色锐化"), QStringLiteral("将多光谱影像与全色影像进行融合。"), {"input", "output", "pan_file", "pan_method"}, {"input", "output", "pan_file"}}},
            {"hough", {QStringLiteral("霍夫变换"), QStringLiteral("检测直线或圆形结构。"), {"input", "output", "band", "hough_type", "hough_threshold", "min_line_length", "max_line_gap", "min_radius", "max_radius", "circle_param2"}, {"input", "output"}}},
            {"watershed", {QStringLiteral("分水岭分割"), QStringLiteral("执行分水岭分割，可选外部标记输入。"), {"input", "output", "band", "marker_input"}, {"input", "output"}}},
            {"kmeans", {QStringLiteral("K-Means 分割"), QStringLiteral("按聚类数对影像全部波段执行 K-Means 分割。"), {"input", "output", "k", "max_iter", "epsilon_kmeans"}, {"input", "output"}}},
        }},
        {"classification", {
            {"feature_stats", {QStringLiteral("地物分类统计"), QStringLiteral("按面要素范围对多源分类栅格执行优先级统计，可输出统计表、分类面和分类栅格。"), {"vector", "class_map", "rasters", "output", "feature_id_field", "feature_name_field", "bands", "nodatas", "target_epsg", "vector_output", "raster_output"}, {"vector", "class_map", "rasters", "output"}}},
        }},
        {"spindex", {
            {"ndvi", {QStringLiteral("NDVI"), QStringLiteral("根据红光与近红外波段计算 NDVI。"), {"input", "output", "red_band", "nir_band"}, {"input", "output"}}},
            {"evi", {QStringLiteral("EVI"), QStringLiteral("根据蓝光、红光与近红外波段计算增强植被指数 EVI。"), {"input", "output", "blue_band", "red_band", "nir_band", "g_value", "c1", "c2", "l_value"}, {"input", "output"}}},
            {"savi", {QStringLiteral("SAVI"), QStringLiteral("根据红光与近红外波段计算土壤调节植被指数 SAVI。"), {"input", "output", "red_band", "nir_band", "l_value"}, {"input", "output"}}},
            {"gndvi", {QStringLiteral("GNDVI"), QStringLiteral("根据绿光与近红外波段计算 GNDVI。"), {"input", "output", "green_band", "nir_band"}, {"input", "output"}}},
            {"ndwi", {QStringLiteral("NDWI"), QStringLiteral("根据绿光与近红外波段计算 NDWI。"), {"input", "output", "green_band", "nir_band"}, {"input", "output"}}},
            {"mndwi", {QStringLiteral("MNDWI"), QStringLiteral("根据绿光与短波红外1波段计算 MNDWI。"), {"input", "output", "green_band", "swir1_band"}, {"input", "output"}}},
            {"ndbi", {QStringLiteral("NDBI"), QStringLiteral("根据短波红外1与近红外波段计算 NDBI。"), {"input", "output", "swir1_band", "nir_band"}, {"input", "output"}}},
            {"custom_index", {QStringLiteral("自定义指数"), QStringLiteral("按表达式组合多波段并输出自定义指数结果。"), {"input", "output", "expression"}, {"input", "output", "expression"}}},
        }},
        {"utility", {
            {"overviews", {QStringLiteral("金字塔"), QStringLiteral("为影像构建多级金字塔，提高浏览性能。"), {"input", "levels", "resample"}, {"input"}}},
            {"nodata", {QStringLiteral("NoData 设置"), QStringLiteral("为单波段或全部波段写入 NoData 值。"), {"input", "band", "nodata_value"}, {"input"}}},
            {"histogram", {QStringLiteral("直方图"), QStringLiteral("计算波段直方图，可选输出为 JSON。"), {"input", "output", "band", "bins"}, {"input"}}},
            {"info", {QStringLiteral("栅格信息"), QStringLiteral("查看栅格驱动、范围、波段和统计信息。"), {"input"}, {"input"}}},
            {"colormap", {QStringLiteral("伪彩色"), QStringLiteral("对单波段影像应用伪彩色映射。"), {"input", "output", "band", "cmap"}, {"input", "output"}}},
        }},
        {"vector", {
            {"info", {QStringLiteral("矢量信息"), QStringLiteral("查看矢量图层、字段和空间参考信息。"), {"input", "layer"}, {"input"}}},
            {"filter", {QStringLiteral("空间过滤"), QStringLiteral("按属性条件或空间范围过滤要素，二者至少填写一个。"), {"input", "output", "layer", "where", "extent"}, {"input", "output"}}},
            {"buffer", {QStringLiteral("缓冲区"), QStringLiteral("为要素生成指定距离的缓冲区。"), {"input", "output", "layer", "distance"}, {"input", "output"}}},
            {"clip", {QStringLiteral("矢量裁切"), QStringLiteral("使用裁切矢量对输入矢量执行裁切。"), {"input", "output", "layer", "clip_vector"}, {"input", "output", "clip_vector"}}},
            {"rasterize", {QStringLiteral("栅格化"), QStringLiteral("按分辨率将矢量图层转换为栅格。"), {"input", "output", "layer", "resolution", "attribute"}, {"input", "output"}}},
            {"polygonize", {QStringLiteral("面矢量化"), QStringLiteral("将栅格指定波段转为矢量面。"), {"input", "output", "band"}, {"input", "output"}}},
            {"convert", {QStringLiteral("格式转换"), QStringLiteral("将矢量数据转换到目标格式。"), {"input", "output", "layer", "format"}, {"input", "output"}}},
            {"union", {QStringLiteral("并集"), QStringLiteral("对输入矢量和叠加矢量执行并集分析。"), {"input", "output", "layer", "overlay_vector"}, {"input", "output", "overlay_vector"}}},
            {"difference", {QStringLiteral("差集"), QStringLiteral("从输入矢量中扣除叠加矢量区域。"), {"input", "output", "layer", "overlay_vector"}, {"input", "output", "overlay_vector"}}},
            {"dissolve", {QStringLiteral("融合"), QStringLiteral("按字段或整体融合相邻要素。"), {"input", "output", "layer", "dissolve_field"}, {"input", "output"}}},
        }},
    };
    return kConfigs;
}

const ActionUiConfig* findActionUiConfig(const std::string& pluginName, const std::string& actionKey) {
    const auto& all = actionUiConfigStorage();
    const auto pluginIt = all.find(pluginName);
    if (pluginIt == all.end()) {
        return nullptr;
    }

    const auto actionIt = pluginIt->second.find(actionKey);
    if (actionIt == pluginIt->second.end()) {
        return nullptr;
    }

    return &actionIt->second;
}

QString actionDisplayName(const std::string& pluginName, const QString& actionKey) {
    if (const auto* config = findActionUiConfig(pluginName, actionKey.toStdString());
        config && !config->displayName.isEmpty()) {
        return config->displayName;
    }
    return genericActionDisplayName(actionKey);
}

const std::map<std::string, std::map<std::string, std::set<std::string>>>& actionVisibilityMapStorage() {
    static const std::map<std::string, std::map<std::string, std::set<std::string>>> kMap = [] {
        std::map<std::string, std::map<std::string, std::set<std::string>>> result;
        for (const auto& [pluginName, actionMap] : actionUiConfigStorage()) {
            for (const auto& [actionKey, config] : actionMap) {
                result[pluginName][actionKey] = config.visibleKeys;
            }
        }
        return result;
    }();
    return kMap;
}

bool isZeroExtent(const std::array<double, 4>& extent) {
    return extent[0] == 0.0 && extent[1] == 0.0 && extent[2] == 0.0 && extent[3] == 0.0;
}

std::string lowerString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> splitCommaList(const std::string& text) {
    std::vector<std::string> items;
    std::istringstream iss(text);
    std::string item;
    while (std::getline(iss, item, ',')) {
        const auto begin = item.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            continue;
        }
        const auto end = item.find_last_not_of(" \t\r\n");
        items.push_back(item.substr(begin, end - begin + 1));
    }
    return items;
}

bool endsWithOneOf(const std::string& path, const std::vector<std::string>& suffixes) {
    const std::string lowerPath = lowerString(path);
    for (const auto& suffix : suffixes) {
        if (lowerPath.size() >= suffix.size() &&
            lowerPath.compare(lowerPath.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<std::array<double, 4>> extentParamValue(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return std::nullopt;
    }
    if (const auto* arr = std::get_if<std::array<double, 4>>(&it->second)) {
        return *arr;
    }
    return std::nullopt;
}

std::optional<double> doubleParamValue(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<double>(&it->second)) {
        return *value;
    }
    if (const auto* value = std::get_if<int>(&it->second)) {
        return static_cast<double>(*value);
    }
    return std::nullopt;
}

std::optional<int> intParamValue(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<int>(&it->second)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<gis::gui::ActionValidationIssue> actionSpecificValidationIssue(
    const std::string& pluginName,
    const std::string& actionKey,
    const std::map<std::string, gis::framework::ParamValue>& params) {
    return gis::gui::validateActionSpecificParams(pluginName, actionKey, params);
}

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    reporter_ = new QtProgressReporter(this);
    setupUi();
    connect(reporter_, &QtProgressReporter::progressChanged, this, [this](double percent) {
        const int value = std::clamp(static_cast<int>(percent), 0, 100);
        if (progressBar_) {
            progressBar_->setRange(0, 100);
            progressBar_->setValue(value);
            progressBar_->setFormat(QStringLiteral("完成 %1%").arg(value));
        }
        if (statusProgressBar_) {
            statusProgressBar_->setRange(0, 100);
            statusProgressBar_->setValue(value);
        }
    });
    connect(reporter_, &QtProgressReporter::messageLogged, this, [this](const QString& message) {
        if (!message.isEmpty() && resultSummaryLabel_) {
            resultSummaryLabel_->setStyleSheet(QString());
            resultSummaryLabel_->setText(message);
        }
    });
    loadPlugins();
}

MainWindow::~MainWindow() = default;

void MainWindow::selectPluginByName(const std::string& pluginName) {
    if (navPanel_) {
        navPanel_->setCurrentPluginSelection(pluginName);
    }
    onPluginSelected(pluginName);
}

void MainWindow::selectActionByKey(const std::string& actionKey) {
    if (navPanel_) {
        navPanel_->setCurrentSubFunctionSelection(actionKey);
    }
    onSubFunctionSelected(actionKey);
}

const std::map<std::string, std::map<std::string, std::set<std::string>>>& MainWindow::actionParamVisibilityMap() {
    return actionVisibilityMapStorage();
}

std::set<std::string> MainWindow::visibleParamsForAction(
    const std::string& pluginName,
    const std::string& actionKey) {
    const auto& all = actionParamVisibilityMap();
    const auto pluginIt = all.find(pluginName);
    if (pluginIt == all.end()) {
        return {};
    }

    const auto actionIt = pluginIt->second.find(actionKey);
    if (actionIt == pluginIt->second.end()) {
        return {};
    }

    return actionIt->second;
}

QString MainWindow::actionDescription(const std::string& pluginName, const QString& actionKey) {
    if (const auto* config = findActionUiConfig(pluginName, actionKey.toStdString())) {
        return config->description;
    }
    return {};
}

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("GIS 工具台"));
    resize(gis::style::Size::kWindowDefaultWidth, gis::style::Size::kWindowDefaultHeight);
    setMinimumSize(gis::style::Size::kWindowMinWidth, gis::style::Size::kWindowMinHeight);
    setStyleSheet(gis::style::globalStyleSheet());

    auto* centralWidget = new QWidget;
    setCentralWidget(centralWidget);

    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    navPanel_ = new NavPanel;
    connect(navPanel_, &NavPanel::pluginSelected, this, &MainWindow::onPluginSelected);
    connect(navPanel_, &NavPanel::subFunctionSelected, this, &MainWindow::onSubFunctionSelected);

    auto* rightPanel = new QWidget;
    rightPanel->setObjectName(QStringLiteral("pagePanel"));

    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(
        20,
        20,
        20,
        18);
    rightLayout->setSpacing(gis::style::Size::kCardSpacing);

    auto* titleCard = new QFrame;
    titleCard->setObjectName(QStringLiteral("heroCard"));
    auto* titleLayout = new QVBoxLayout(titleCard);
    titleLayout->setContentsMargins(
        22,
        20,
        22,
        20);
    titleLayout->setSpacing(10);

    auto* headerTopLayout = new QHBoxLayout;
    headerTopLayout->setContentsMargins(0, 0, 0, 0);
    headerTopLayout->setSpacing(10);

    auto* heroBadgeLabel = new QLabel(QStringLiteral("算法工作台"));
    heroBadgeLabel->setObjectName(QStringLiteral("heroBadge"));
    headerTopLayout->addWidget(heroBadgeLabel, 0, Qt::AlignLeft);
    headerTopLayout->addStretch();
    titleLayout->addLayout(headerTopLayout);

    auto* heroMainLayout = new QHBoxLayout;
    heroMainLayout->setContentsMargins(0, 0, 0, 0);
    heroMainLayout->setSpacing(12);

    functionIconLabel_ = new QLabel;
    functionIconLabel_->setObjectName(QStringLiteral("heroIconBadge"));
    functionIconLabel_->setAlignment(Qt::AlignCenter);
    functionIconLabel_->setPixmap(badgeIconPixmap(QStringLiteral("default"), QColor("#EAF3FF"), QColor("#2F7CF6")));
    heroMainLayout->addWidget(functionIconLabel_, 0, Qt::AlignTop);

    auto* heroTextLayout = new QVBoxLayout;
    heroTextLayout->setContentsMargins(0, 0, 0, 0);
    heroTextLayout->setSpacing(2);

    functionTitleLabel_ = new QLabel(QStringLiteral("请选择功能"));
    functionTitleLabel_->setObjectName(QStringLiteral("heroTitle"));
    heroTextLayout->addWidget(functionTitleLabel_);

    functionDescLabel_ = new QLabel(QStringLiteral("从左侧选择插件和子功能后，这里会显示功能说明和参数配置。"));
    functionDescLabel_->setObjectName(QStringLiteral("heroDesc"));
    functionDescLabel_->setWordWrap(true);
    heroTextLayout->addWidget(functionDescLabel_);

    functionMetaLabel_ = new QLabel(QStringLiteral("当前状态：等待选择主功能"));
    functionMetaLabel_->setObjectName(QStringLiteral("heroMeta"));
    heroTextLayout->addWidget(functionMetaLabel_);

    heroMainLayout->addLayout(heroTextLayout, 1);
    titleLayout->addLayout(heroMainLayout);

    rightLayout->addWidget(titleCard);

    auto* paramScrollArea = new QScrollArea;
    paramScrollArea->setWidgetResizable(true);
    paramScrollArea->setFrameShape(QFrame::NoFrame);
    paramScrollArea->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    paramWidget_ = new ParamWidget;
    connect(paramWidget_, &ParamWidget::paramsChanged, this, &MainWindow::onParamValuesChanged);
    paramScrollArea->setWidget(paramWidget_);

    rightLayout->addWidget(paramScrollArea, 1);

    auto* executionCard = new QFrame;
    executionCard->setObjectName(QStringLiteral("execCard"));
    auto* executionLayout = new QVBoxLayout(executionCard);
    executionLayout->setContentsMargins(
        18,
        18,
        18,
        18);
    executionLayout->setSpacing(12);

    auto* execHeaderLayout = new QHBoxLayout;
    execHeaderLayout->setSpacing(12);

    auto* execTitleLabel = new QLabel(QStringLiteral("执行控制"));
    execTitleLabel->setObjectName(QStringLiteral("cardTitle"));
    execHeaderLayout->addWidget(execTitleLabel);
    execHeaderLayout->addStretch();

    statusExecutionLabel_ = new QLabel(QStringLiteral("就绪"));
    statusExecutionLabel_->setObjectName(QStringLiteral("statusBadgeReady"));
    execHeaderLayout->addWidget(statusExecutionLabel_);

    executeButton_ = new QPushButton(QStringLiteral("执行处理"));
    executeButton_->setObjectName(QStringLiteral("primaryButton"));
    executeButton_->setIcon(executeIcon());
    executeButton_->setIconSize(QSize(16, 16));
    executeButton_->setEnabled(false);
    connect(executeButton_, &QPushButton::clicked, this, &MainWindow::onExecute);
    execHeaderLayout->addWidget(executeButton_);

    executionLayout->addLayout(execHeaderLayout);

    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    progressBar_->setFormat(QStringLiteral("等待执行"));
    executionLayout->addWidget(progressBar_);

    resultSummaryLabel_ = new QLabel;
    resultSummaryLabel_->setWordWrap(true);
    resultSummaryLabel_->setObjectName(QStringLiteral("execSummary"));
    resultSummaryLabel_->setMinimumHeight(28);
    resultSummaryLabel_->setText(QStringLiteral("当前未执行任务。选择子功能并补全参数后，可以直接开始运行。"));
    executionLayout->addWidget(resultSummaryLabel_);

    rightLayout->addWidget(executionCard);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);
    splitter->addWidget(navPanel_);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({gis::style::Size::kSidebarWidth, gis::style::Size::kWindowDefaultWidth - gis::style::Size::kSidebarWidth});

    mainLayout->addWidget(splitter);

    statusPluginCountLabel_ = new QLabel(QStringLiteral("已加载主功能：0"));
    statusAlgorithmLabel_ = new QLabel(QStringLiteral("当前算法：未选择"));
    statusSubFunctionCountLabel_ = new QLabel(QStringLiteral("已加载子功能：0"));
    statusProgressBar_ = new QProgressBar;
    statusProgressBar_->setRange(0, 100);
    statusProgressBar_->setValue(0);
    statusProgressBar_->setFixedWidth(180);
    statusProgressBar_->setTextVisible(false);
    statusPluginCountLabel_->setObjectName(QStringLiteral("statusBarLabel"));
    statusAlgorithmLabel_->setObjectName(QStringLiteral("statusBarLabel"));
    statusSubFunctionCountLabel_->setObjectName(QStringLiteral("statusBarLabel"));
    statusBar()->addPermanentWidget(statusPluginCountLabel_);
    statusBar()->addPermanentWidget(statusAlgorithmLabel_);
    statusBar()->addPermanentWidget(statusSubFunctionCountLabel_);
    statusBar()->addPermanentWidget(statusProgressBar_);
    statusBar()->showMessage(QStringLiteral("就绪"));
}

void MainWindow::loadPlugins() {
    namespace fs = std::filesystem;

    const auto exePath = fs::canonical(fs::path(QApplication::applicationFilePath().toStdWString()).parent_path());
    const auto pluginsDir = gis::core::findPluginDirectoryFrom(exePath);
    if (!pluginsDir.empty()) {
        pluginManager_.loadFromDirectory(pluginsDir.string());
    }

    static const std::vector<std::string> preferredOrder = {
        "projection", "cutting", "matching", "processing", "classification", "utility", "vector"
    };

    std::vector<gis::framework::IGisPlugin*> plugins = pluginManager_.plugins();
    std::sort(plugins.begin(), plugins.end(), [&](auto* lhs, auto* rhs) {
        const auto leftIt = std::find(preferredOrder.begin(), preferredOrder.end(), lhs->name());
        const auto rightIt = std::find(preferredOrder.begin(), preferredOrder.end(), rhs->name());
        return leftIt < rightIt;
    });

    navPanel_->setPlugins(plugins);

    if (plugins.empty()) {
        statusBar()->showMessage(QStringLiteral("未找到插件，请检查 plugins 目录"));
        if (statusPluginCountLabel_) {
            statusPluginCountLabel_->setText(QStringLiteral("已加载主功能：0"));
        }
        if (statusSubFunctionCountLabel_) {
            statusSubFunctionCountLabel_->setText(QStringLiteral("已加载子功能：0"));
        }
        return;
    }

    if (statusPluginCountLabel_) {
        statusPluginCountLabel_->setText(QStringLiteral("已加载主功能：%1").arg(plugins.size()));
    }
    if (statusSubFunctionCountLabel_) {
        statusSubFunctionCountLabel_->setText(QStringLiteral("已加载子功能：0"));
    }
    statusBar()->showMessage(QStringLiteral("已加载 %1 个插件").arg(plugins.size()));
}

std::vector<gis::framework::ParamSpec> MainWindow::effectiveParamSpecs() const {
    if (!currentPlugin_ || currentActionKey_.isEmpty()) {
        return {};
    }

    const auto* config = findActionUiConfig(
        currentPlugin_->name(), currentActionKey_.toStdString());
    auto visibleKeys = visibleParamsForAction(
        currentPlugin_->name(), currentActionKey_.toStdString());
    const std::set<std::string> requiredKeys = config ? config->requiredKeys : std::set<std::string>{};
    auto filtered = gis::gui::buildEffectiveGuiParamSpecs(
        currentPlugin_->name(),
        currentActionKey_.toStdString(),
        currentPlugin_->paramSpecs(),
        visibleKeys,
        requiredKeys);
    for (auto& adjustedSpec : filtered) {
        if (const auto it = commonParamTextStorage().find(adjustedSpec.key); it != commonParamTextStorage().end()) {
            adjustedSpec.displayName = it->second.displayName.toUtf8().toStdString();
            adjustedSpec.description = it->second.description.toUtf8().toStdString();
        }
        if (const auto* actionText = findActionSpecificParamText(
                currentPlugin_->name(), currentActionKey_.toStdString(), adjustedSpec.key)) {
            adjustedSpec.displayName = actionText->displayName.toUtf8().toStdString();
            adjustedSpec.description = actionText->description.toUtf8().toStdString();
        }
    }
    return filtered;
}

std::map<std::string, gis::framework::ParamValue> MainWindow::collectExecutionParams() const {
    auto params = paramWidget_ ? paramWidget_->collectParams() : std::map<std::string, gis::framework::ParamValue>{};
    if (!currentActionKey_.isEmpty()) {
        params["action"] = currentActionKey_.toStdString();
    }
    return params;
}

bool MainWindow::setParamValue(const std::string& key, const std::string& value) {
    if (!paramWidget_ || !paramWidget_->hasParam(key)) {
        return false;
    }
    const bool applied = paramWidget_->setValueFromString(key, value);
    if (applied) {
        syncDerivedParams();
        refreshExecuteButtonState();
        refreshParamValidationState();
    }
    return applied;
}

void MainWindow::triggerExecute() {
    onExecute();
}

bool MainWindow::lastExecutionSuccess() const {
    return lastExecutionSuccess_;
}

bool MainWindow::lastExecutionCancelled() const {
    return lastExecutionCancelled_;
}

QString MainWindow::lastExecutionMessage() const {
    return lastExecutionMessage_;
}

QString MainWindow::lastExecutionRawMessage() const {
    return lastExecutionRawMessage_;
}

void MainWindow::onPluginSelected(const std::string& pluginName) {
    resetDerivedParamTracking();
    currentPlugin_ = pluginManager_.find(pluginName);
    if (!currentPlugin_) {
        paramWidget_->clear();
        currentActionKey_.clear();
        functionTitleLabel_->setText(QStringLiteral("请选择功能"));
        functionDescLabel_->setText(QStringLiteral("从左侧选择插件和子功能后，这里会显示功能说明和参数配置。"));
        if (functionIconLabel_) {
            functionIconLabel_->setPixmap(badgeIconPixmap(QStringLiteral("default"), QColor("#EAF3FF"), QColor("#2F7CF6")));
        }
        if (functionMetaLabel_) {
            functionMetaLabel_->setText(QStringLiteral("当前状态：等待选择主功能"));
        }
        if (statusAlgorithmLabel_) {
            statusAlgorithmLabel_->setText(QStringLiteral("当前算法：未选择"));
        }
        if (statusSubFunctionCountLabel_) {
            statusSubFunctionCountLabel_->setText(QStringLiteral("已加载子功能：0"));
        }
        navPanel_->clearSubFunctions();
        refreshExecuteButtonState();
        return;
    }

    functionTitleLabel_->setText(QString::fromUtf8(currentPlugin_->displayName()));
    functionDescLabel_->setText(QString::fromUtf8(currentPlugin_->description()));
    if (functionIconLabel_) {
        functionIconLabel_->setPixmap(badgeIconPixmap(QString::fromStdString(currentPlugin_->name()), QColor("#EAF3FF"), QColor("#2F7CF6")));
    }
    if (functionMetaLabel_) {
        functionMetaLabel_->setText(
            QStringLiteral("当前主功能：%1  |  子功能数：载入中")
                .arg(QString::fromUtf8(currentPlugin_->displayName())));
    }
    if (statusAlgorithmLabel_) {
        statusAlgorithmLabel_->setText(
            QStringLiteral("当前算法：%1").arg(QString::fromUtf8(currentPlugin_->displayName())));
    }

    std::vector<std::string> actions;
    std::vector<std::string> displayNames;
    for (const auto& spec : currentPlugin_->paramSpecs()) {
        if (spec.key != "action") continue;
        for (const auto& action : spec.enumValues) {
            actions.push_back(action);
            displayNames.push_back(
                actionDisplayName(currentPlugin_->name(), QString::fromStdString(action)).toUtf8().toStdString());
        }
        break;
    }
    navPanel_->setSubFunctions(actions, displayNames);
    navPanel_->setCurrentPluginSelection(currentPlugin_->name());
    if (statusSubFunctionCountLabel_) {
        statusSubFunctionCountLabel_->setText(
            QStringLiteral("已加载子功能：%1").arg(static_cast<int>(actions.size())));
    }
    if (functionMetaLabel_) {
        functionMetaLabel_->setText(
            QStringLiteral("当前主功能：%1  |  子功能数：%2")
                .arg(QString::fromUtf8(currentPlugin_->displayName()))
                .arg(static_cast<int>(actions.size())));
    }

    currentActionKey_.clear();
    paramWidget_->clear();
    if (resultSummaryLabel_) {
        resultSummaryLabel_->setStyleSheet(QString());
        resultSummaryLabel_->setText(QStringLiteral("请选择该主功能下的子功能，然后补全参数。"));
    }
    refreshExecuteButtonState();
    statusBar()->showMessage(QStringLiteral("当前主功能：%1").arg(QString::fromUtf8(currentPlugin_->displayName())));
}

void MainWindow::onSubFunctionSelected(const std::string& actionKey) {
    if (!currentPlugin_) {
        paramWidget_->clear();
        refreshExecuteButtonState();
        return;
    }

    resetDerivedParamTracking();
    currentActionKey_ = QString::fromStdString(actionKey);
    navPanel_->setCurrentSubFunctionSelection(actionKey);
    if (functionIconLabel_) {
        functionIconLabel_->setPixmap(badgeIconPixmap(actionIconText(currentActionKey_), QColor("#EAF3FF"), QColor("#2F7CF6")));
    }

    QString displayName = actionDisplayName(currentPlugin_->name(), currentActionKey_);
    functionTitleLabel_->setText(displayName);

    QString desc = actionDescription(currentPlugin_->name(), currentActionKey_);
    functionDescLabel_->setText(desc.isEmpty()
        ? QString::fromUtf8(currentPlugin_->description()) : desc);
    if (functionMetaLabel_) {
        functionMetaLabel_->setText(
            QStringLiteral("当前主功能：%1  |  当前子功能：%2")
                .arg(QString::fromUtf8(currentPlugin_->displayName()))
                .arg(displayName));
    }

    paramWidget_->setUiContext(currentPlugin_->name(), actionKey);
    paramWidget_->setParamSpecs(effectiveParamSpecs());
    if (resultSummaryLabel_) {
        resultSummaryLabel_->setStyleSheet(QString());
        resultSummaryLabel_->setText(QStringLiteral("参数面板已刷新，补全必填项后即可执行当前子功能。"));
    }
    syncDerivedParams();
    refreshExecuteButtonState();
    refreshParamValidationState();

    if (statusExecutionLabel_) {
        statusExecutionLabel_->setObjectName(QStringLiteral("statusBadgeReady"));
        statusExecutionLabel_->style()->unpolish(statusExecutionLabel_);
        statusExecutionLabel_->style()->polish(statusExecutionLabel_);
        statusExecutionLabel_->setText(QStringLiteral("待执行"));
    }
    statusBar()->showMessage(QStringLiteral("当前子功能：%1").arg(displayName));
}

void MainWindow::onExecute() {
    if (!currentPlugin_) {
        QMessageBox::warning(this, QStringLiteral("\346\217\220\347\244\272"),
                             QStringLiteral("请先选择一个主功能"));
        return;
    }
    if (currentActionKey_.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\346\217\220\347\244\272"),
                             QStringLiteral("请先选择一个子功能"));
        return;
    }

    const auto specs = effectiveParamSpecs();
    auto params = collectExecutionParams();
    std::string validationMessage = gis::gui::validateExecutionParams(specs, params);
    if (validationMessage.empty()) {
        if (const auto issue = actionSpecificValidationIssue(
                currentPlugin_->name(), currentActionKey_.toStdString(), params)) {
            validationMessage = issue->message;
        }
    }
    if (!validationMessage.empty()) {
        refreshParamValidationState();
        QMessageBox::warning(this, QStringLiteral("参数未完成"),
                             QString::fromUtf8(validationMessage));
        return;
    }

    runPluginWithParams(params);
}

void MainWindow::onParamValuesChanged() {
    if (isSyncingParams_) return;
    syncDerivedParams();
    refreshExecuteButtonState();
    refreshParamValidationState();
}

void MainWindow::refreshExecuteButtonState() {
    if (!executeButton_) return;

    const bool hasSelection = currentPlugin_ && !currentActionKey_.isEmpty();
    std::string validationMessage;
    if (hasSelection) {
        const auto params = collectExecutionParams();
        validationMessage = gis::gui::validateExecutionParams(
            effectiveParamSpecs(),
            params);
        if (validationMessage.empty()) {
            if (const auto issue = actionSpecificValidationIssue(
                    currentPlugin_->name(), currentActionKey_.toStdString(), params)) {
                validationMessage = issue->message;
            }
        }
    }

    const auto state = gis::gui::buildExecuteButtonState(hasSelection, validationMessage);
    executeButton_->setEnabled(state.enabled);
    executeButton_->setToolTip(QString::fromUtf8(state.tooltip));
    if (statusExecutionLabel_) {
        statusExecutionLabel_->setObjectName(QString::fromUtf8(state.statusObjectName));
        statusExecutionLabel_->style()->unpolish(statusExecutionLabel_);
        statusExecutionLabel_->style()->polish(statusExecutionLabel_);
        statusExecutionLabel_->setText(QString::fromUtf8(state.statusText));
    }
}

void MainWindow::refreshParamValidationState() {
    if (!paramWidget_) {
        return;
    }
    const bool hasSelection = currentPlugin_ && !currentActionKey_.isEmpty();
    if (!hasSelection) {
        if (paramWidget_) {
            paramWidget_->setHighlightedParam({});
        }
        return;
    }

    const auto specs = effectiveParamSpecs();
    const auto params = collectExecutionParams();
    const auto issue = actionSpecificValidationIssue(
        currentPlugin_->name(), currentActionKey_.toStdString(), params);
    paramWidget_->setHighlightedParam(
        gis::gui::resolveHighlightedParamKey(hasSelection, specs, params, issue));
}

void MainWindow::syncDerivedParams() {
    if (!paramWidget_ || !currentPlugin_ || currentActionKey_.isEmpty()) {
        return;
    }

    const auto actionKey = currentActionKey_.toStdString();
    auto params = collectExecutionParams();
    const std::string inputPath = paramWidget_->stringValue("input");
    const std::string referencePath = paramWidget_->stringValue("reference");
    const std::string primaryPath = !inputPath.empty() ? inputPath : referencePath;

    isSyncingParams_ = true;

    auto syncOutputField = [&](const std::string& key, std::string& lastAutoPath) {
        if (!paramWidget_->hasParam(key) || primaryPath.empty()) {
            return;
        }
        const std::string currentValue = paramWidget_->stringValue(key);
        const std::string formatValue = key == "output" ? paramWidget_->stringValue("format") : std::string{};
        const auto update = gis::gui::computeDerivedOutputUpdate(
            currentValue,
            lastAutoPath,
            primaryPath,
            currentPlugin_->name(),
            actionKey,
            key,
            formatValue);
        if (update.shouldApply) {
            paramWidget_->setStringValue(key, update.value);
        }
        lastAutoPath = update.autoValue;
    };

    syncOutputField("output", lastAutoOutputPath_);
    syncOutputField("vector_output", lastAutoVectorOutputPath_);
    syncOutputField("raster_output", lastAutoRasterOutputPath_);

    if (!inputPath.empty()) {
        const auto info = gis::gui::inspectDataForAutoFill(inputPath);
        const QString inputPathLower = QString::fromStdString(inputPath).toLower();
        const std::string currentLayer = paramWidget_->stringValue("layer");
        if (paramWidget_->hasParam("layer")
            && !info.layerName.empty()
            && !inputPathLower.endsWith(QStringLiteral(".shp"))) {
            if (gis::gui::shouldAutoFillLayerValue(currentLayer, lastAutoLayerName_, info.layerName)) {
                paramWidget_->setStringValue("layer", info.layerName);
            }
            lastAutoLayerName_ = info.layerName;
        }
        if (paramWidget_->hasParam("extent")) {
            const auto extent = extentParamValue(params, "extent");
            if (gis::gui::shouldAutoFillExtentValue(extent, lastAutoExtent_, info.hasExtent)) {
                paramWidget_->setExtentValue("extent", info.extent);
                lastAutoExtent_ = info.extent;
            }
        }
    }

    isSyncingParams_ = false;
}

void MainWindow::resetDerivedParamTracking() {
    lastAutoOutputPath_.clear();
    lastAutoVectorOutputPath_.clear();
    lastAutoRasterOutputPath_.clear();
    lastAutoLayerName_.clear();
    lastAutoExtent_.reset();
}

void MainWindow::runPluginWithParams(
    const std::map<std::string, gis::framework::ParamValue>& params) {
    reporter_->reset();
    lastExecutionSuccess_ = false;
    lastExecutionCancelled_ = false;
    lastExecutionMessage_.clear();
    lastExecutionRawMessage_.clear();
    if (resultSummaryLabel_) {
        resultSummaryLabel_->setStyleSheet(QString());
        resultSummaryLabel_->setText(QStringLiteral("正在执行，请稍候..."));
    }
    if (statusExecutionLabel_) {
        statusExecutionLabel_->setObjectName(QStringLiteral("statusBadgeRunning"));
        statusExecutionLabel_->style()->unpolish(statusExecutionLabel_);
        statusExecutionLabel_->style()->polish(statusExecutionLabel_);
        statusExecutionLabel_->setText(QStringLiteral("运行中"));
    }
    if (progressBar_) {
        progressBar_->setRange(0, 0);
        progressBar_->setFormat(QStringLiteral("处理中"));
    }
    if (statusProgressBar_) {
        statusProgressBar_->setRange(0, 0);
    }
    executeButton_->setEnabled(false);

    auto* worker = new ExecuteWorker;
    worker->setup(currentPlugin_, params, reporter_);

    auto* thread = new QThread;
    worker->moveToThread(thread);

    auto* progressDialog = new ProgressDialog(reporter_);

    connect(thread, &QThread::started, worker, &ExecuteWorker::run);
    connect(worker, &ExecuteWorker::finished, this,
            [this, progressDialog](const gis::framework::Result& result) {
                const QString localizedMessage =
                    QString::fromUtf8(gis::gui::localizeResultMessage(result.message));
                QString message = localizedMessage;
                const bool cancelled = result.message == "\345\267\262\345\217\226\346\266\210\346\211\247\350\241\214";
                lastExecutionSuccess_ = result.success;
                lastExecutionCancelled_ = cancelled;
                lastExecutionMessage_ = localizedMessage;
                lastExecutionRawMessage_ = QString::fromUtf8(result.message);
                progressDialog->setFinished(message, result.success, cancelled);

                if (result.success) {
                    QString summary = QString::fromUtf8(gis::gui::buildResultSummaryText(result));
                    resultSummaryLabel_->setText(
                        QStringLiteral("✓ 执行成功\n%1").arg(summary));
                    resultSummaryLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(gis::style::Color::kSuccess));
                    if (progressBar_) {
                        progressBar_->setRange(0, 100);
                        progressBar_->setValue(100);
                        progressBar_->setFormat(QStringLiteral("完成 100%"));
                    }
                    if (statusExecutionLabel_) {
                        statusExecutionLabel_->setObjectName(QStringLiteral("statusBadgeSuccess"));
                        statusExecutionLabel_->style()->unpolish(statusExecutionLabel_);
                        statusExecutionLabel_->style()->polish(statusExecutionLabel_);
                        statusExecutionLabel_->setText(QStringLiteral("成功"));
                    }
                    statusBar()->showMessage(QStringLiteral("执行成功"));
                } else if (cancelled) {
                    resultSummaryLabel_->setText(QStringLiteral("✖ 已取消"));
                    resultSummaryLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(gis::style::Color::kWarning));
                    if (progressBar_) {
                        progressBar_->setRange(0, 100);
                        progressBar_->setValue(0);
                        progressBar_->setFormat(QStringLiteral("已取消"));
                    }
                    if (statusExecutionLabel_) {
                        statusExecutionLabel_->setObjectName(QStringLiteral("statusBadgeWarning"));
                        statusExecutionLabel_->style()->unpolish(statusExecutionLabel_);
                        statusExecutionLabel_->style()->polish(statusExecutionLabel_);
                        statusExecutionLabel_->setText(QStringLiteral("已取消"));
                    }
                    statusBar()->showMessage(QStringLiteral("执行已取消"));
                } else {
                    resultSummaryLabel_->setText(
                        QStringLiteral("✖ 执行失败\n%1").arg(message));
                    resultSummaryLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(gis::style::Color::kError));
                    if (progressBar_) {
                        progressBar_->setRange(0, 100);
                        progressBar_->setValue(0);
                        progressBar_->setFormat(QStringLiteral("失败"));
                    }
                    if (statusExecutionLabel_) {
                        statusExecutionLabel_->setObjectName(QStringLiteral("statusBadgeError"));
                        statusExecutionLabel_->style()->unpolish(statusExecutionLabel_);
                        statusExecutionLabel_->style()->polish(statusExecutionLabel_);
                        statusExecutionLabel_->setText(QStringLiteral("失败"));
                    }
                    statusBar()->showMessage(QStringLiteral("执行失败：") + message);
                }

                if (statusProgressBar_) {
                    statusProgressBar_->setRange(0, 100);
                    statusProgressBar_->setValue(result.success ? 100 : 0);
                }
                refreshExecuteButtonState();
                emit executionFinished(result.success);
            });
    connect(worker, &ExecuteWorker::finished, thread, &QThread::quit);
    connect(worker, &ExecuteWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(progressDialog, &QDialog::finished, progressDialog, &QObject::deleteLater);

    thread->start();
    progressDialog->exec();
}


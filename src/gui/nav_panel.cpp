#include "nav_panel.h"
#include "style_constants.h"

#include <gis/framework/plugin.h>

#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QString collapsedPluginText(const std::string& pluginName, const QString& displayName) {
    Q_UNUSED(pluginName);
    return QStringLiteral("%1   >").arg(displayName);
}

QString expandedPluginText(const std::string& pluginName, const QString& displayName) {
    Q_UNUSED(pluginName);
    return QStringLiteral("%1   v").arg(displayName);
}

QString subFunctionText(const QString& displayName, bool active) {
    Q_UNUSED(active);
    return displayName;
}

QIcon makeSidebarIcon(const std::string& kind, const QColor& bg, const QColor& fg) {
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 17, 17), 5, 5);

    QPen pen(fg);
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    if (kind == "processing") {
        painter.drawRect(QRectF(4.2, 4.2, 9.6, 9.6));
        painter.drawLine(QPointF(5.6, 11.8), QPointF(8.2, 9.2));
        painter.drawLine(QPointF(8.2, 9.2), QPointF(10.2, 10.8));
        painter.drawLine(QPointF(10.2, 10.8), QPointF(12.4, 7.4));
    } else if (kind == "raster_math") {
        painter.drawRect(QRectF(4.2, 4.2, 9.6, 9.6));
        painter.drawLine(QPointF(9.0, 4.2), QPointF(9.0, 13.8));
        painter.drawLine(QPointF(4.2, 9.0), QPointF(13.8, 9.0));
        painter.drawLine(QPointF(5.4, 5.4), QPointF(6.8, 6.8));
        painter.drawLine(QPointF(6.8, 5.4), QPointF(5.4, 6.8));
    } else if (kind == "raster_inspect") {
        painter.drawLine(QPointF(5, 12.8), QPointF(5, 9.4));
        painter.drawLine(QPointF(9, 12.8), QPointF(9, 6.6));
        painter.drawLine(QPointF(13, 12.8), QPointF(13, 4.4));
        painter.drawEllipse(QRectF(4.0, 7.4, 1.8, 1.8));
        painter.drawEllipse(QRectF(8.0, 5.2, 1.8, 1.8));
        painter.drawEllipse(QRectF(12.0, 3.0, 1.8, 1.8));
    } else if (kind == "raster_manage") {
        painter.drawRect(QRectF(4.2, 4.2, 9.6, 9.6));
        painter.drawLine(QPointF(6.0, 9.0), QPointF(12.0, 9.0));
        painter.drawLine(QPointF(9.0, 6.0), QPointF(9.0, 12.0));
    } else if (kind == "terrain") {
        painter.drawPolyline(QPolygonF() << QPointF(3.4, 12.4) << QPointF(6.8, 8.0) << QPointF(9.4, 9.8) << QPointF(13.6, 4.6));
    } else if (kind == "projection") {
        painter.drawEllipse(QRectF(4, 4, 10, 10));
        painter.drawLine(QPointF(9, 4), QPointF(9, 14));
        painter.drawLine(QPointF(4, 9), QPointF(14, 9));
    } else if (kind == "cutting") {
        painter.drawLine(QPointF(5.2, 5.2), QPointF(12.8, 12.8));
        painter.drawLine(QPointF(12.8, 5.2), QPointF(5.2, 12.8));
        painter.drawEllipse(QRectF(3.9, 3.9, 2.8, 2.8));
        painter.drawEllipse(QRectF(11.3, 3.9, 2.8, 2.8));
    } else if (kind == "matching") {
        painter.drawEllipse(QRectF(4.2, 4.2, 9.6, 9.6));
        painter.drawLine(QPointF(9, 4.6), QPointF(9, 13.4));
        painter.drawLine(QPointF(4.6, 9), QPointF(13.4, 9));
        painter.drawEllipse(QRectF(7.3, 7.3, 3.4, 3.4));
    } else if (kind == "classification") {
        painter.drawRect(QRectF(4.0, 4.0, 10.0, 10.0));
        painter.drawLine(QPointF(9.0, 4.0), QPointF(9.0, 14.0));
        painter.drawLine(QPointF(4.0, 9.0), QPointF(14.0, 9.0));
        painter.drawPoint(QPointF(6.5, 6.5));
        painter.drawPoint(QPointF(11.5, 6.5));
        painter.drawPoint(QPointF(6.5, 11.5));
    } else if (kind == "raster_render") {
        painter.drawLine(QPointF(5, 12.8), QPointF(5, 9.4));
        painter.drawLine(QPointF(9, 12.8), QPointF(9, 6.6));
        painter.drawLine(QPointF(13, 12.8), QPointF(13, 4.4));
    } else if (kind == "spindex") {
        painter.drawEllipse(QRectF(4.4, 3.4, 7.2, 10.2));
        painter.drawLine(QPointF(8.0, 4.8), QPointF(8.0, 12.0));
        painter.drawLine(QPointF(8.0, 8.0), QPointF(12.6, 4.6));
    } else if (kind == "vector") {
        painter.drawEllipse(QRectF(4.2, 4.2, 2.6, 2.6));
        painter.drawEllipse(QRectF(10.8, 4.8, 2.6, 2.6));
        painter.drawEllipse(QRectF(8.0, 10.8, 2.6, 2.6));
        painter.drawLine(QPointF(6.4, 6.4), QPointF(10.8, 6.8));
        painter.drawLine(QPointF(11.2, 7.4), QPointF(9.6, 10.8));
        painter.drawLine(QPointF(8.4, 10.8), QPointF(6.0, 6.8));
    } else {
        painter.drawEllipse(QRectF(6, 6, 6, 6));
    }
    return QIcon(pixmap);
}

QIcon makeSubFunctionIcon(const std::string& actionKey, bool active) {
    const QColor stroke = active ? QColor("#FFFFFF") : QColor("#9AA8B8");
    const QColor fill = active ? QColor("#5CB8FF") : QColor(Qt::transparent);

    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(stroke);
    pen.setWidthF(1.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(fill);

    if (actionKey == "threshold") {
        painter.drawRect(QRectF(2.6, 2.6, 10.8, 10.8));
        painter.drawLine(QPointF(4.0, 11.8), QPointF(6.8, 8.9));
        painter.drawLine(QPointF(6.8, 8.9), QPointF(9.0, 10.3));
        painter.drawLine(QPointF(9.0, 10.3), QPointF(11.8, 5.5));
    } else if (actionKey == "transform" || actionKey == "reproject") {
        painter.drawEllipse(QRectF(2.6, 2.6, 10.8, 10.8));
        painter.drawLine(QPointF(8, 2.8), QPointF(8, 13.2));
        painter.drawLine(QPointF(2.8, 8), QPointF(13.2, 8));
        if (actionKey == "reproject") {
            painter.drawLine(QPointF(4.4, 4.4), QPointF(11.6, 11.6));
        } else {
            painter.drawEllipse(QRectF(6.1, 6.1, 3.8, 3.8));
        }
    } else if (actionKey == "assign_srs") {
        painter.drawRoundedRect(QRectF(2.8, 4.0, 10.4, 8.4), 2, 2);
        painter.drawLine(QPointF(5.0, 6.4), QPointF(11.0, 6.4));
        painter.drawLine(QPointF(5.0, 9.2), QPointF(9.6, 9.2));
        painter.drawLine(QPointF(8.0, 2.4), QPointF(8.0, 4.0));
    } else if (actionKey == "info" || actionKey == "stats" || actionKey == "histogram") {
        painter.drawLine(QPointF(4.0, 12.2), QPointF(4.0, 8.0));
        painter.drawLine(QPointF(8.0, 12.2), QPointF(8.0, 6.0));
        painter.drawLine(QPointF(12.0, 12.2), QPointF(12.0, 4.0));
        if (actionKey != "histogram") {
            painter.drawEllipse(QRectF(3.2, 5.4, 1.8, 1.8));
            painter.drawEllipse(QRectF(7.2, 7.2, 1.8, 1.8));
            painter.drawEllipse(QRectF(11.2, 3.8, 1.8, 1.8));
        }
    } else if (actionKey == "clip" || actionKey == "buffer") {
        if (actionKey == "clip") {
            painter.drawEllipse(QRectF(2.8, 2.8, 3.2, 3.2));
            painter.drawEllipse(QRectF(10.0, 2.8, 3.2, 3.2));
            painter.drawLine(QPointF(5.0, 5.0), QPointF(11.0, 11.0));
            painter.drawLine(QPointF(11.0, 5.0), QPointF(5.0, 11.0));
        } else {
            painter.drawEllipse(QRectF(5.0, 5.0, 6.0, 6.0));
            painter.drawEllipse(QRectF(2.8, 2.8, 10.4, 10.4));
        }
    } else if (actionKey == "mosaic" || actionKey == "split" || actionKey == "rasterize") {
        painter.drawRect(QRectF(2.8, 2.8, 10.4, 10.4));
        painter.drawLine(QPointF(8.0, 2.8), QPointF(8.0, 13.2));
        painter.drawLine(QPointF(2.8, 8.0), QPointF(13.2, 8.0));
        if (actionKey == "mosaic") {
            painter.drawLine(QPointF(2.8, 2.8), QPointF(13.2, 13.2));
        }
    } else if (actionKey == "merge_bands") {
        painter.drawRect(QRectF(3.2, 5.0, 7.6, 6.0));
        painter.drawRect(QRectF(5.0, 3.4, 7.6, 6.0));
        painter.drawRect(QRectF(6.8, 2.2, 5.0, 5.0));
    } else if (actionKey == "detect") {
        painter.drawEllipse(QRectF(3.4, 3.4, 9.2, 9.2));
        painter.drawLine(QPointF(8.0, 1.8), QPointF(8.0, 4.2));
        painter.drawLine(QPointF(8.0, 11.8), QPointF(8.0, 14.2));
        painter.drawLine(QPointF(1.8, 8.0), QPointF(4.2, 8.0));
        painter.drawLine(QPointF(11.8, 8.0), QPointF(14.2, 8.0));
    } else if (actionKey == "filter" || actionKey == "enhance" || actionKey == "band_math") {
        if (actionKey == "filter") {
            painter.drawEllipse(QRectF(2.6, 2.6, 10.8, 10.8));
            painter.drawLine(QPointF(3.2, 8), QPointF(12.8, 8));
        } else if (actionKey == "enhance") {
            painter.drawLine(QPointF(8.0, 2.8), QPointF(8.0, 13.2));
            painter.drawLine(QPointF(2.8, 8.0), QPointF(13.2, 8.0));
            painter.drawLine(QPointF(4.4, 4.4), QPointF(11.6, 11.6));
        } else {
            painter.drawLine(QPointF(8.0, 2.8), QPointF(8.0, 13.2));
            painter.drawLine(QPointF(2.8, 8.0), QPointF(13.2, 8.0));
            painter.drawLine(QPointF(4.4, 4.4), QPointF(5.9, 5.9));
            painter.drawLine(QPointF(5.9, 4.4), QPointF(4.4, 5.9));
        }
    } else if (actionKey == "match" || actionKey == "register" || actionKey == "ecc_register") {
        if (actionKey == "match") {
            painter.drawEllipse(QRectF(2.6, 2.6, 4.4, 4.4));
            painter.drawEllipse(QRectF(9.0, 9.0, 4.4, 4.4));
            painter.drawLine(QPointF(6.2, 6.2), QPointF(9.8, 9.8));
        } else {
            painter.drawRect(QRectF(2.8, 4.0, 5.2, 5.2));
            painter.drawRect(QRectF(7.8, 6.6, 5.2, 5.2));
            painter.drawLine(QPointF(6.0, 2.4), QPointF(10.0, 2.4));
            painter.drawLine(QPointF(10.0, 2.4), QPointF(8.8, 1.4));
            painter.drawLine(QPointF(10.0, 2.4), QPointF(8.8, 3.4));
        }
    } else if (actionKey == "change") {
        painter.drawRect(QRectF(2.8, 4.0, 4.4, 4.4));
        painter.drawRect(QRectF(8.8, 7.6, 4.4, 4.4));
        painter.drawLine(QPointF(6.8, 6.8), QPointF(9.4, 9.4));
    } else if (actionKey == "corner") {
        painter.drawLine(QPointF(3.2, 3.2), QPointF(3.2, 12.8));
        painter.drawLine(QPointF(3.2, 12.8), QPointF(12.8, 12.8));
        painter.drawEllipse(QRectF(6.6, 6.6, 2.8, 2.8));
    } else if (actionKey == "stitch") {
        painter.drawRoundedRect(QRectF(2.8, 4.6, 5.2, 6.0), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(7.8, 3.2, 5.2, 6.0), 1.5, 1.5);
        painter.drawLine(QPointF(7.8, 7.6), QPointF(7.8, 7.6));
    } else if (actionKey == "feature_stats") {
        painter.drawRect(QRectF(2.8, 2.8, 10.4, 10.4));
        painter.drawLine(QPointF(8.0, 2.8), QPointF(8.0, 13.2));
        painter.drawLine(QPointF(2.8, 8.0), QPointF(13.2, 8.0));
        painter.drawPoint(QPointF(5.0, 5.0));
        painter.drawPoint(QPointF(11.0, 5.0));
        painter.drawPoint(QPointF(5.0, 11.0));
    } else if (actionKey == "edge") {
        painter.drawPolyline(QPolygonF() << QPointF(3.0, 11.5) << QPointF(6.0, 4.5) << QPointF(8.4, 9.8) << QPointF(13.0, 3.2));
    } else if (actionKey == "contour") {
        painter.drawEllipse(QRectF(2.8, 2.8, 10.4, 10.4));
        painter.drawEllipse(QRectF(5.2, 5.2, 5.6, 5.6));
    } else if (actionKey == "template_match") {
        painter.drawRect(QRectF(2.8, 2.8, 10.4, 10.4));
        painter.drawRect(QRectF(5.6, 5.6, 4.8, 4.8));
    } else if (actionKey == "pansharpen") {
        painter.drawEllipse(QRectF(3.6, 3.6, 4.8, 4.8));
        painter.drawLine(QPointF(11.0, 4.0), QPointF(11.0, 12.8));
        painter.drawLine(QPointF(9.2, 8.4), QPointF(12.8, 8.4));
    } else if (actionKey == "hough") {
        painter.drawLine(QPointF(3.0, 12.6), QPointF(13.0, 2.6));
        painter.drawEllipse(QRectF(7.6, 5.2, 4.8, 4.8));
    } else if (actionKey == "watershed") {
        painter.drawEllipse(QRectF(5.0, 3.0, 6.0, 8.4));
        painter.drawLine(QPointF(8.0, 11.4), QPointF(8.0, 13.4));
    } else if (actionKey == "kmeans") {
        painter.drawEllipse(QRectF(2.8, 2.8, 2.8, 2.8));
        painter.drawEllipse(QRectF(10.2, 3.4, 2.8, 2.8));
        painter.drawEllipse(QRectF(6.6, 10.0, 2.8, 2.8));
        painter.drawLine(QPointF(5.4, 5.4), QPointF(10.2, 5.8));
        painter.drawLine(QPointF(11.2, 6.6), QPointF(8.6, 10.4));
        painter.drawLine(QPointF(7.4, 10.2), QPointF(4.8, 6.0));
    } else if (actionKey == "overviews") {
        painter.drawRect(QRectF(2.8, 2.8, 10.4, 10.4));
        painter.drawRect(QRectF(4.6, 4.6, 6.8, 6.8));
        painter.drawRect(QRectF(6.4, 6.4, 3.2, 3.2));
    } else if (actionKey == "nodata") {
        painter.drawEllipse(QRectF(3.2, 3.2, 9.6, 9.6));
        painter.drawLine(QPointF(4.0, 12.0), QPointF(12.0, 4.0));
    } else if (actionKey == "colormap") {
        painter.drawRoundedRect(QRectF(2.8, 4.0, 10.4, 8.0), 2.5, 2.5);
        painter.drawLine(QPointF(5.4, 6.4), QPointF(5.4, 9.8));
        painter.drawLine(QPointF(8.0, 5.2), QPointF(8.0, 10.8));
        painter.drawLine(QPointF(10.6, 6.4), QPointF(10.6, 9.8));
    } else if (actionKey == "slope") {
        painter.drawPolyline(QPolygonF() << QPointF(2.8, 12.0) << QPointF(6.0, 7.4) << QPointF(9.2, 8.8) << QPointF(13.2, 3.8));
    } else if (actionKey == "aspect") {
        painter.drawEllipse(QRectF(2.8, 2.8, 10.4, 10.4));
        painter.drawLine(QPointF(8.0, 8.0), QPointF(11.6, 4.4));
        painter.drawLine(QPointF(11.6, 4.4), QPointF(9.6, 4.4));
        painter.drawLine(QPointF(11.6, 4.4), QPointF(11.6, 6.4));
    } else if (actionKey == "hillshade") {
        painter.drawPolyline(QPolygonF() << QPointF(2.8, 12.0) << QPointF(6.0, 7.4) << QPointF(9.2, 8.8) << QPointF(13.2, 3.8));
        painter.drawEllipse(QRectF(2.6, 2.6, 2.4, 2.4));
    } else if (actionKey == "tpi") {
        painter.drawPolyline(QPolygonF() << QPointF(2.8, 12.0) << QPointF(5.4, 7.4) << QPointF(8.0, 9.4) << QPointF(10.8, 5.8) << QPointF(13.2, 8.6));
    } else if (actionKey == "roughness") {
        painter.drawPolyline(QPolygonF() << QPointF(2.8, 12.0) << QPointF(5.0, 5.2) << QPointF(8.0, 11.0) << QPointF(10.4, 4.6) << QPointF(13.2, 9.8));
    } else if (actionKey == "fill_sinks") {
        painter.drawPolyline(QPolygonF() << QPointF(2.8, 8.0) << QPointF(5.0, 11.8) << QPointF(8.0, 13.2) << QPointF(10.8, 10.2) << QPointF(13.2, 5.4));
    } else if (actionKey == "flow_direction") {
        painter.drawLine(QPointF(3.4, 12.0), QPointF(12.2, 4.8));
        painter.drawLine(QPointF(12.2, 4.8), QPointF(9.6, 4.8));
        painter.drawLine(QPointF(12.2, 4.8), QPointF(12.2, 7.4));
    } else if (actionKey == "flow_accumulation") {
        painter.drawLine(QPointF(3.6, 4.8), QPointF(8.0, 9.2));
        painter.drawLine(QPointF(12.4, 4.8), QPointF(8.0, 9.2));
        painter.drawLine(QPointF(8.0, 9.2), QPointF(8.0, 13.0));
    } else if (actionKey == "stream_extract") {
        painter.drawLine(QPointF(3.6, 4.8), QPointF(8.0, 9.2));
        painter.drawLine(QPointF(12.4, 4.8), QPointF(8.0, 9.2));
        painter.drawLine(QPointF(8.0, 9.2), QPointF(8.0, 13.0));
        painter.drawEllipse(QRectF(6.8, 11.0, 2.4, 2.4));
    } else if (actionKey == "ndvi") {
        painter.drawEllipse(QRectF(4.2, 2.8, 7.0, 10.4));
        painter.drawLine(QPointF(7.8, 4.0), QPointF(7.8, 12.0));
        painter.drawLine(QPointF(7.8, 7.4), QPointF(11.6, 4.0));
    } else if (actionKey == "spindex") {
        painter.drawEllipse(QRectF(4.2, 2.8, 7.0, 10.4));
        painter.drawLine(QPointF(7.8, 4.0), QPointF(7.8, 12.0));
        painter.drawLine(QPointF(7.8, 7.4), QPointF(11.6, 4.0));
    } else if (actionKey == "convert") {
        painter.drawLine(QPointF(3.2, 5.2), QPointF(12.8, 5.2));
        painter.drawLine(QPointF(9.8, 3.2), QPointF(12.8, 5.2));
        painter.drawLine(QPointF(9.8, 7.2), QPointF(12.8, 5.2));
        painter.drawLine(QPointF(12.8, 10.8), QPointF(3.2, 10.8));
        painter.drawLine(QPointF(6.2, 8.8), QPointF(3.2, 10.8));
        painter.drawLine(QPointF(6.2, 12.8), QPointF(3.2, 10.8));
    } else if (actionKey == "union") {
        painter.drawEllipse(QRectF(2.8, 4.0, 5.6, 5.6));
        painter.drawEllipse(QRectF(7.6, 4.0, 5.6, 5.6));
    } else if (actionKey == "difference") {
        painter.drawEllipse(QRectF(2.8, 4.0, 6.4, 6.4));
        painter.drawLine(QPointF(11.0, 7.2), QPointF(13.2, 7.2));
    } else if (actionKey == "vector" || actionKey == "polygonize" || actionKey == "dissolve") {
        if (actionKey == "polygonize") {
            painter.drawPolygon(QPolygonF() << QPointF(3.6, 5.6) << QPointF(7.8, 3.0) << QPointF(13.0, 6.2) << QPointF(11.2, 11.8) << QPointF(5.0, 13.0));
        } else if (actionKey == "dissolve") {
            painter.drawEllipse(QRectF(2.8, 4.0, 4.2, 4.2));
            painter.drawEllipse(QRectF(8.2, 4.0, 4.2, 4.2));
            painter.drawEllipse(QRectF(5.4, 8.2, 4.2, 4.2));
        } else {
            painter.drawEllipse(QRectF(2.8, 2.8, 2.8, 2.8));
            painter.drawEllipse(QRectF(10.2, 3.4, 2.8, 2.8));
            painter.drawEllipse(QRectF(6.6, 10.0, 2.8, 2.8));
            painter.drawLine(QPointF(5.4, 5.4), QPointF(10.2, 5.8));
            painter.drawLine(QPointF(11.2, 6.6), QPointF(8.6, 10.4));
            painter.drawLine(QPointF(7.4, 10.2), QPointF(4.8, 6.0));
        }
    } else {
        painter.drawEllipse(QRectF(4.6, 4.6, 6.8, 6.8));
    }

    return QIcon(pixmap);
}

}

NavPanel::NavPanel(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void NavPanel::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    sidebarFrame_ = new QFrame;
    sidebarFrame_->setObjectName(QStringLiteral("sidebar"));
    sidebarFrame_->setStyleSheet(gis::style::sidebarStyleSheet());
    sidebarFrame_->setFixedWidth(gis::style::Size::kSidebarWidth);
    sidebarFrame_->setMinimumWidth(gis::style::Size::kSidebarMinWidth);

    auto* sidebarLayout = new QVBoxLayout(sidebarFrame_);
    sidebarLayout->setContentsMargins(14, 10, 14, 10);
    sidebarLayout->setSpacing(8);
    sidebarLayout_ = sidebarLayout;

    auto* topCard = new QFrame;
    topCard->setObjectName(QStringLiteral("sidebarTopCard"));
    auto* topLayout = new QVBoxLayout(topCard);
    topLayout->setContentsMargins(4, 6, 4, 6);
    topLayout->setSpacing(3);

    auto* eyebrowLabel = new QLabel(QStringLiteral("GIS TOOLKIT"));
    eyebrowLabel->setObjectName(QStringLiteral("sidebarEyebrow"));
    topLayout->addWidget(eyebrowLabel);

    titleLabel_ = new QLabel(QStringLiteral("GIS 工具台"));
    titleLabel_->setObjectName(QStringLiteral("sidebarTitle"));
    topLayout->addWidget(titleLabel_);

    auto* descLabel = new QLabel(QStringLiteral("点击主功能后在原位展开子功能，参数配置与执行反馈集中在同一界面。"));
    descLabel->setObjectName(QStringLiteral("sidebarDesc"));
    descLabel->setWordWrap(true);
    topLayout->addWidget(descLabel);
    sidebarLayout->addWidget(topCard);

    scrollArea_ = new QScrollArea;
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* middleContainer = new QWidget;
    middleContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* middleLayout = new QVBoxLayout(middleContainer);
    middleLayout->setContentsMargins(0, 0, 4, 0);
    middleLayout->setSpacing(10);

    auto* sectionLabel = new QLabel(QStringLiteral("功能分类"));
    sectionLabel->setObjectName(QStringLiteral("sidebarSection"));
    middleLayout->addWidget(sectionLabel);

    auto* pluginContainer = new QWidget;
    pluginContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    pluginLayout_ = new QVBoxLayout(pluginContainer);
    pluginLayout_->setContentsMargins(0, 0, 0, 0);
    pluginLayout_->setSpacing(6);
    middleLayout->addWidget(pluginContainer);

    auto* separator = new QFrame;
    separator->setObjectName(QStringLiteral("sidebarDivider"));
    middleLayout->addWidget(separator);

    auto* moreLabel = new QLabel(QStringLiteral("更多工具"));
    moreLabel->setObjectName(QStringLiteral("sidebarSection"));
    middleLayout->addWidget(moreLabel);

    middleLayout->addStretch();
    scrollArea_->setWidget(middleContainer);
    sidebarLayout->addWidget(scrollArea_, 1);

    auto* footerCard = new QFrame;
    footerCard->setObjectName(QStringLiteral("sidebarFooterCard"));
    auto* footerLayout = new QVBoxLayout(footerCard);
    footerLayout->setContentsMargins(4, 6, 4, 4);
    footerLayout->setSpacing(3);

    auto* footerTitle = new QLabel(QStringLiteral("更多工具"));
    footerTitle->setObjectName(QStringLiteral("sidebarFooterTitle"));
    footerLayout->addWidget(footerTitle);

    auto* footerDesc = new QLabel(QStringLiteral("当前先聚焦算法执行，后续可继续补结果预览、批处理和检查能力。"));
    footerDesc->setObjectName(QStringLiteral("sidebarFooterDesc"));
    footerDesc->setWordWrap(true);
    footerLayout->addWidget(footerDesc);
    sidebarLayout->addWidget(footerCard);

    rootLayout->addWidget(sidebarFrame_);
}

void NavPanel::setPlugins(const std::vector<gis::framework::IGisPlugin*>& plugins) {
    QLayoutItem* item = nullptr;
    while ((item = pluginLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    pluginGroupMap_.clear();
    pluginSubContainerMap_.clear();
    pluginSubLayoutMap_.clear();
    pluginDisplayNameMap_.clear();
    pluginButtonMap_.clear();
    subFunctionButtonMap_.clear();
    subFunctionDisplayNameMap_.clear();
    currentPluginButton_ = nullptr;
    currentSubFunctionButton_ = nullptr;

    for (auto* plugin : plugins) {
        auto* groupWidget = new QWidget;
        groupWidget->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* groupLayout = new QVBoxLayout(groupWidget);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(2);

        const QString displayName = QString::fromUtf8(plugin->displayName());

        auto* btn = new QPushButton;
        btn->setObjectName(QStringLiteral("navItem"));
        btn->setCheckable(true);
        btn->setText(collapsedPluginText(plugin->name(), displayName));
        btn->setIcon(makeSidebarIcon(plugin->name(), QColor("#2F7CF6"), QColor("#FFFFFF")));
        btn->setIconSize(QSize(18, 18));
        connect(btn, &QPushButton::clicked, this, [this, name = plugin->name()]() {
            onPluginButtonClicked(name);
        });
        groupLayout->addWidget(btn);

        auto* subContainer = new QWidget;
        subContainer->setObjectName(QStringLiteral("subFunctionContainer"));
        subContainer->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* subLayout = new QVBoxLayout(subContainer);
        subLayout->setContentsMargins(10, 0, 0, 0);
        subLayout->setSpacing(4);
        subContainer->hide();
        groupLayout->addWidget(subContainer);

        pluginLayout_->addWidget(groupWidget);
        pluginGroupMap_[plugin->name()] = groupWidget;
        pluginSubContainerMap_[plugin->name()] = subContainer;
        pluginSubLayoutMap_[plugin->name()] = subLayout;
        pluginDisplayNameMap_[plugin->name()] = displayName;
        pluginButtonMap_[btn] = plugin->name();
    }
}

void NavPanel::clearSubFunctions() {
    for (auto& entry : pluginSubLayoutMap_) {
        QLayoutItem* item = nullptr;
        while ((item = entry.second->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
    }
    subFunctionButtonMap_.clear();
    subFunctionDisplayNameMap_.clear();
    currentSubFunctionButton_ = nullptr;
}

void NavPanel::setSubFunctions(const std::vector<std::string>& actions,
                               const std::vector<std::string>& displayNames) {
    if (!currentPluginButton_) {
        clearSubFunctions();
        return;
    }

    const auto pluginIt = pluginButtonMap_.find(currentPluginButton_);
    if (pluginIt == pluginButtonMap_.end()) {
        clearSubFunctions();
        return;
    }

    const auto subLayoutIt = pluginSubLayoutMap_.find(pluginIt->second);
    const auto subContainerIt = pluginSubContainerMap_.find(pluginIt->second);
    if (subLayoutIt == pluginSubLayoutMap_.end() || subContainerIt == pluginSubContainerMap_.end()) {
        clearSubFunctions();
        return;
    }

    clearSubFunctions();

    for (size_t i = 0; i < actions.size(); ++i) {
        const QString displayText = QString::fromUtf8(i < displayNames.size() ? displayNames[i] : actions[i]);

        auto* btn = new QPushButton;
        btn->setObjectName(QStringLiteral("subNavItem"));
        btn->setCheckable(true);
        btn->setText(subFunctionText(displayText, false));
        btn->setIcon(makeSubFunctionIcon(actions[i], false));
        btn->setIconSize(QSize(16, 16));

        connect(btn, &QPushButton::clicked, this, [this, action = actions[i]]() {
            onSubFunctionButtonClicked(action);
        });

        subLayoutIt->second->addWidget(btn);
        subFunctionButtonMap_[btn] = actions[i];
        subFunctionDisplayNameMap_[btn] = displayText;
    }

    subContainerIt->second->setVisible(!actions.empty());
}

void NavPanel::setCurrentPluginSelection(const std::string& pluginName) {
    if (pluginName.empty()) {
        for (auto& entry : pluginButtonMap_) {
            entry.first->setChecked(false);
            const QString displayName = pluginDisplayNameMap_[entry.second];
            entry.first->setText(collapsedPluginText(entry.second, displayName));
            const auto subContainerIt = pluginSubContainerMap_.find(entry.second);
            if (subContainerIt != pluginSubContainerMap_.end()) {
                subContainerIt->second->setVisible(false);
            }
        }
        currentPluginButton_ = nullptr;
        return;
    }

    for (auto& entry : pluginButtonMap_) {
        const bool active = entry.second == pluginName;
        entry.first->setChecked(active);
        const QString displayName = pluginDisplayNameMap_[entry.second];
        entry.first->setText(active
            ? expandedPluginText(entry.second, displayName)
            : collapsedPluginText(entry.second, displayName));

        const auto subContainerIt = pluginSubContainerMap_.find(entry.second);
        if (subContainerIt != pluginSubContainerMap_.end()) {
            subContainerIt->second->setVisible(active && subContainerIt->second->layout() && subContainerIt->second->layout()->count() > 0);
        }

        if (active) {
            currentPluginButton_ = entry.first;
        }
    }

    if (scrollArea_ && currentPluginButton_) {
        scrollArea_->ensureWidgetVisible(currentPluginButton_, 0, 24);
    }
}

void NavPanel::setCurrentSubFunctionSelection(const std::string& actionKey) {
    if (actionKey.empty()) {
        for (auto& entry : subFunctionButtonMap_) {
            entry.first->setChecked(false);
            const QString displayName = subFunctionDisplayNameMap_[entry.first];
            entry.first->setText(subFunctionText(displayName, false));
            entry.first->setIcon(makeSubFunctionIcon(entry.second, false));
        }
        currentSubFunctionButton_ = nullptr;
        return;
    }

    for (auto& entry : subFunctionButtonMap_) {
        const bool active = entry.second == actionKey;
        entry.first->setChecked(active);
        const QString displayName = subFunctionDisplayNameMap_[entry.first];
        entry.first->setText(subFunctionText(displayName, active));
        entry.first->setIcon(makeSubFunctionIcon(entry.second, active));
        if (active) {
            currentSubFunctionButton_ = entry.first;
        }
    }

    if (scrollArea_ && currentSubFunctionButton_) {
        scrollArea_->ensureWidgetVisible(currentSubFunctionButton_, 0, 36);
    }
}

void NavPanel::onPluginButtonClicked(const std::string& pluginName) {
    auto* clickedButton = qobject_cast<QPushButton*>(sender());
    if (clickedButton) {
        const auto clickedIt = pluginButtonMap_.find(clickedButton);
        if (clickedIt != pluginButtonMap_.end() &&
            clickedIt->second == pluginName &&
            !clickedButton->isChecked()) {
            setCurrentPluginSelection(std::string{});
            setCurrentSubFunctionSelection(std::string{});
            emit pluginSelected(std::string{});
            return;
        }
    }

    setCurrentPluginSelection(pluginName);
    emit pluginSelected(pluginName);
}

void NavPanel::onSubFunctionButtonClicked(const std::string& actionKey) {
    setCurrentSubFunctionSelection(actionKey);
    emit subFunctionSelected(actionKey);
}

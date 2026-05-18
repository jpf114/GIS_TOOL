#include "vector_preview_widget.h"
#include "style_constants.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

namespace {

GDALDataset* openVectorDataset(const QString& path) {
    return static_cast<GDALDataset*>(
        GDALOpenEx(path.toUtf8().constData(),
                   GDAL_OF_VECTOR | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
}

}

VectorPreviewWidget::VectorPreviewWidget(QWidget* parent)
    : QWidget(parent) {
    buildUi();
}

void VectorPreviewWidget::buildUi() {
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

    auto* outlineCard = new QFrame;
    outlineCard->setObjectName(QStringLiteral("card"));
    auto* outlineLayout = new QVBoxLayout(outlineCard);
    outlineLayout->setContentsMargins(18, 14, 18, 14);
    outlineLayout->setSpacing(8);

    auto* outlineTitle = new QLabel(QStringLiteral("几何轮廓"));
    outlineTitle->setObjectName(QStringLiteral("cardTitle"));
    outlineLayout->addWidget(outlineTitle);

    outlineLabel_ = new QLabel;
    outlineLabel_->setAlignment(Qt::AlignCenter);
    outlineLabel_->setMinimumSize(200, 150);
    outlineLabel_->setStyleSheet(
        QStringLiteral("background: #1E2A36; border-radius: 6px; color: %1;")
            .arg(gis::style::Color::kTextMuted));
    outlineLabel_->setText(QStringLiteral("无数据"));
    outlineLayout->addWidget(outlineLabel_);

    contentLayout->addWidget(outlineCard);

    auto* infoCard = new QFrame;
    infoCard->setObjectName(QStringLiteral("card"));
    auto* infoLayout = new QVBoxLayout(infoCard);
    infoLayout->setContentsMargins(18, 14, 18, 14);
    infoLayout->setSpacing(8);

    auto* infoTitle = new QLabel(QStringLiteral("要素信息"));
    infoTitle->setObjectName(QStringLiteral("cardTitle"));
    infoLayout->addWidget(infoTitle);

    infoLabel_ = new QLabel;
    infoLabel_->setObjectName(QStringLiteral("cardDesc"));
    infoLabel_->setWordWrap(true);
    infoLabel_->setTextFormat(Qt::PlainText);
    infoLabel_->setText(QStringLiteral("无数据"));
    infoLayout->addWidget(infoLabel_);

    contentLayout->addWidget(infoCard);

    auto* attrCard = new QFrame;
    attrCard->setObjectName(QStringLiteral("card"));
    auto* attrLayout = new QVBoxLayout(attrCard);
    attrLayout->setContentsMargins(18, 14, 18, 14);
    attrLayout->setSpacing(8);

    auto* attrTitle = new QLabel(QStringLiteral("属性表预览"));
    attrTitle->setObjectName(QStringLiteral("cardTitle"));
    attrLayout->addWidget(attrTitle);

    attrTable_ = new QTableWidget;
    attrTable_->setAlternatingRowColors(true);
    attrTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    attrTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    attrTable_->horizontalHeader()->setStretchLastSection(true);
    attrTable_->verticalHeader()->setDefaultSectionSize(28);
    attrTable_->setMaximumHeight(300);
    attrLayout->addWidget(attrTable_);

    contentLayout->addWidget(attrCard);
    contentLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    mainLayout->addWidget(scrollArea);
}

void VectorPreviewWidget::loadVector(const QString& path) {
    clear();
    filePath_ = path;
    loadOutline();
    loadFeatureInfo();
    loadAttributeTable();
}

void VectorPreviewWidget::clear() {
    filePath_.clear();
    outlineLabel_->setPixmap(QPixmap());
    outlineLabel_->setText(QStringLiteral("无数据"));
    infoLabel_->setText(QStringLiteral("无数据"));
    attrTable_->clear();
    attrTable_->setRowCount(0);
    attrTable_->setColumnCount(0);
}

void VectorPreviewWidget::loadOutline() {
    auto* ds = openVectorDataset(filePath_);
    if (!ds) {
        outlineLabel_->setText(QStringLiteral("无法打开矢量文件"));
        return;
    }

    OGRLayer* layer = ds->GetLayer(0);
    if (!layer) {
        GDALClose(ds);
        outlineLabel_->setText(QStringLiteral("无图层"));
        return;
    }

    OGREnvelope env;
    layer->GetExtent(&env, TRUE);
    double envW = env.MaxX - env.MinX;
    double envH = env.MaxY - env.MinY;
    if (envW <= 0 || envH <= 0) {
        GDALClose(ds);
        outlineLabel_->setText(QStringLiteral("范围无效"));
        return;
    }

    QPixmap pixmap(kOutlineSize, kOutlineSize);
    pixmap.fill(QColor(30, 42, 54));
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double scale = std::min(static_cast<double>(kOutlineSize - 40) / envW,
                            static_cast<double>(kOutlineSize - 40) / envH);
    double offsetX = (kOutlineSize - envW * scale) / 2.0;
    double offsetY = (kOutlineSize - envH * scale) / 2.0;

    auto mapX = [&](double x) { return offsetX + (x - env.MinX) * scale; };
    auto mapY = [&](double y) { return kOutlineSize - offsetY - (y - env.MinY) * scale; };

    QPen pen(QColor("#5CB8FF"), 1.5);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(QColor(92, 184, 255, 30));

    layer->ResetReading();
    OGRFeature* feat;
    int count = 0;
    while ((feat = layer->GetNextFeature()) != nullptr && count < 500) {
        auto* geom = feat->GetGeometryRef();
        if (!geom) {
            OGRFeature::DestroyFeature(feat);
            continue;
        }

        switch (wkbFlatten(geom->getGeometryType())) {
        case wkbPoint: {
            auto* pt = geom->toPoint();
            double x = mapX(pt->getX());
            double y = mapY(pt->getY());
            painter.drawEllipse(QPointF(x, y), 3, 3);
            break;
        }
        case wkbLineString: {
            auto* ls = geom->toLineString();
            QPolygonF poly;
            for (int i = 0; i < ls->getNumPoints(); ++i) {
                poly << QPointF(mapX(ls->getX(i)), mapY(ls->getY(i)));
            }
            painter.drawPolyline(poly);
            break;
        }
        case wkbPolygon: {
            auto* poly = geom->toPolygon();
            auto* ring = poly->getExteriorRing();
            if (!ring) break;
            QPolygonF qpoly;
            for (int i = 0; i < ring->getNumPoints(); ++i) {
                qpoly << QPointF(mapX(ring->getX(i)), mapY(ring->getY(i)));
            }
            painter.drawPolygon(qpoly);
            break;
        }
        case wkbMultiPolygon: {
            auto* mp = geom->toMultiPolygon();
            for (int p = 0; p < mp->getNumGeometries(); ++p) {
                auto* subPoly = mp->getGeometryRef(p)->toPolygon();
                if (!subPoly) continue;
                auto* ring = subPoly->getExteriorRing();
                if (!ring) continue;
                QPolygonF qpoly;
                for (int i = 0; i < ring->getNumPoints(); ++i) {
                    qpoly << QPointF(mapX(ring->getX(i)), mapY(ring->getY(i)));
                }
                painter.drawPolygon(qpoly);
            }
            break;
        }
        case wkbMultiLineString: {
            auto* mls = geom->toMultiLineString();
            for (int l = 0; l < mls->getNumGeometries(); ++l) {
                auto* ls = mls->getGeometryRef(l)->toLineString();
                if (!ls) continue;
                QPolygonF poly;
                for (int i = 0; i < ls->getNumPoints(); ++i) {
                    poly << QPointF(mapX(ls->getX(i)), mapY(ls->getY(i)));
                }
                painter.drawPolyline(poly);
            }
            break;
        }
        default:
            break;
        }

        OGRFeature::DestroyFeature(feat);
        count++;
    }

    GDALClose(ds);
    outlineLabel_->setPixmap(pixmap);
}

void VectorPreviewWidget::loadFeatureInfo() {
    auto* ds = openVectorDataset(filePath_);
    if (!ds) {
        infoLabel_->setText(QStringLiteral("无法打开矢量文件"));
        return;
    }

    OGRLayer* layer = ds->GetLayer(0);
    if (!layer) {
        GDALClose(ds);
        infoLabel_->setText(QStringLiteral("无图层"));
        return;
    }

    auto* layerDefn = layer->GetLayerDefn();
    GIntBig featureCount = layer->GetFeatureCount(FALSE);
    QString geomType = QString::fromUtf8(OGRGeometryTypeToName(layerDefn->GetGeomType()));
    int fieldCount = layerDefn->GetFieldCount();

    QString text;
    text += QStringLiteral("文件路径: %1\n").arg(filePath_);
    text += QStringLiteral("图层名: %1\n").arg(QString::fromUtf8(layer->GetName()));
    text += QStringLiteral("要素数量: %1\n").arg(featureCount);
    text += QStringLiteral("几何类型: %1\n").arg(geomType);
    text += QStringLiteral("属性字段数: %1\n").arg(fieldCount);

    auto* srs = layer->GetSpatialRef();
    if (srs) {
        const char* authName = srs->GetAuthorityName(nullptr);
        const char* authCode = srs->GetAuthorityCode(nullptr);
        if (authName && authCode) {
            text += QStringLiteral("坐标系: %1:%2\n").arg(authName).arg(authCode);
        } else {
            char* wkt = nullptr;
            srs->exportToWkt(&wkt);
            if (wkt) {
                text += QStringLiteral("坐标系: %1\n").arg(QString::fromUtf8(wkt).left(80));
                CPLFree(wkt);
            }
        }
    } else {
        text += QStringLiteral("坐标系: 未定义\n");
    }

    OGREnvelope env;
    if (layer->GetExtent(&env, TRUE) == OGRERR_NONE) {
        text += QStringLiteral("范围: [%1, %2, %3, %4]")
                    .arg(env.MinX, 0, 'g', 10)
                    .arg(env.MinY, 0, 'g', 10)
                    .arg(env.MaxX, 0, 'g', 10)
                    .arg(env.MaxY, 0, 'g', 10);
    }

    infoLabel_->setText(text);
    GDALClose(ds);
}

void VectorPreviewWidget::loadAttributeTable() {
    auto* ds = openVectorDataset(filePath_);
    if (!ds) {
        return;
    }

    OGRLayer* layer = ds->GetLayer(0);
    if (!layer) {
        GDALClose(ds);
        return;
    }

    auto* layerDefn = layer->GetLayerDefn();
    int fieldCount = layerDefn->GetFieldCount();

    attrTable_->setColumnCount(fieldCount + 1);
    QStringList headers;
    headers << QStringLiteral("FID");
    for (int i = 0; i < fieldCount; ++i) {
        headers << QString::fromUtf8(layerDefn->GetFieldDefn(i)->GetNameRef());
    }
    attrTable_->setHorizontalHeaderLabels(headers);

    layer->ResetReading();
    OGRFeature* feat;
    int row = 0;
    while ((feat = layer->GetNextFeature()) != nullptr && row < kMaxPreviewRows) {
        attrTable_->insertRow(row);
        attrTable_->setItem(row, 0, new QTableWidgetItem(QString::number(feat->GetFID())));
        for (int col = 0; col < fieldCount; ++col) {
            attrTable_->setItem(row, col + 1,
                new QTableWidgetItem(QString::fromUtf8(feat->GetFieldAsString(col))));
        }
        OGRFeature::DestroyFeature(feat);
        row++;
    }

    if (row >= kMaxPreviewRows) {
        GIntBig total = layer->GetFeatureCount(FALSE);
        if (total > kMaxPreviewRows) {
            attrTable_->insertRow(row);
            auto* spanItem = new QTableWidgetItem(
                QStringLiteral("... 显示前 %1 行，共 %2 行 ...").arg(kMaxPreviewRows).arg(total));
            spanItem->setFlags(spanItem->flags() & ~Qt::ItemIsSelectable);
            attrTable_->setSpan(row, 0, 1, fieldCount + 1);
            attrTable_->setItem(row, 0, spanItem);
        }
    }

    GDALClose(ds);
}

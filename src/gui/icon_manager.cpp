#include "icon_manager.h"

#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QTextStream>

namespace gis::gui {

namespace {

std::string cacheKey(const std::string& svgName, IconWeight weight, int size, const QColor& color) {
    return svgName + "_" + (weight == IconWeight::Bold ? "bold" : "regular") + "_"
         + std::to_string(size) + "_" + color.name().toStdString();
}

} // namespace

IconManager& IconManager::instance() {
    static IconManager mgr;
    return mgr;
}

IconManager::IconManager() {
    loadMapping();
}

void IconManager::setIconsBasePath(const std::string& path) {
    basePath_ = path;
    cache_.clear();
}

void IconManager::loadMapping() {
    pluginMap_["projection"]     = {"globe-hemisphere-west", IconWeight::Bold, QColor("#FFFFFF")};
    pluginMap_["cutting"]        = {"scissors", IconWeight::Bold, QColor("#FFFFFF")};
    pluginMap_["matching"]       = {"crosshair", IconWeight::Bold, QColor("#FFFFFF")};
    pluginMap_["processing"]     = {"sliders-horizontal", IconWeight::Bold, QColor("#FFFFFF")};
    pluginMap_["raster_tools"]   = {"grid-four", IconWeight::Bold, QColor("#FFFFFF")};
    pluginMap_["georef"]         = {"sun", IconWeight::Bold, QColor("#FFFFFF")};
    pluginMap_["terrain"]        = {"mountains", IconWeight::Bold, QColor("#FFFFFF")};
    pluginMap_["classification"] = {"squares-four", IconWeight::Bold, QColor("#FFFFFF")};
    pluginMap_["spindex"]        = {"leaf", IconWeight::Bold, QColor("#FFFFFF")};
    pluginMap_["vector"]         = {"polygon", IconWeight::Bold, QColor("#FFFFFF")};

    actionMap_["info"]         = {"info", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["transform"]    = {"arrows-clockwise", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["assign_srs"]   = {"map-pin", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["reproject"]    = {"globe", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["clip"]         = {"scissors", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["mosaic"]       = {"squares-four", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["split"]        = {"layout", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["merge_bands"]  = {"stack", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["detect"]       = {"magnifying-glass-plus", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["match"]        = {"link", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["register"]     = {"arrows-in-line-horizontal", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["change"]       = {"arrows-left-right", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["ecc_register"] = {"arrows-in-cardinal", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["corner"]       = {"dots-three-outline", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["stitch"]       = {"images", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["threshold"]           = {"chart-line-up", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["filter"]              = {"funnel", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["enhance"]             = {"sparkle", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["stats"]               = {"chart-bar", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["edge"]                = {"wave-sawtooth", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["contour"]             = {"circle", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["template_match"]      = {"copy", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["pansharpen"]          = {"circle-half-tilt", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["hough"]               = {"line-segment", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["watershed"]           = {"drop", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["skeleton"]            = {"tree-structure", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["kmeans"]              = {"circles-three-plus", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["gabor_filter"]        = {"waves", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["glcm_texture"]        = {"checkerboard", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["mean_shift_filter"]   = {"circles-four", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["connected_components"] = {"circles-three", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["band_math"]   = {"math-operations", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["histogram"]   = {"chart-bar", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["overviews"]   = {"stack", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["nodata"]      = {"prohibit", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["cog"]         = {"cloud-arrow-up", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["colormap"]         = {"palette", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["histogram_match"]  = {"chart-bar-horizontal", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["dos_correction"]          = {"cloud-sun", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["radiometric_calibration"] = {"gauge", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["gcp_register"]            = {"push-pin", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["cosine_correction"]       = {"sun-dim", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["minnaert_correction"]     = {"sun-horizon", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["c_correction"]            = {"sun", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["percentile_stretch"]       = {"cloud-fog", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["rpc_orthorectify"]        = {"airplane-tilt", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["slope"]            = {"trend-up", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["aspect"]           = {"compass", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["hillshade"]        = {"mountains", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["tpi"]              = {"chart-line", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["curvature"]        = {"wave-sine", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["profile_curvature"] = {"wave-sine", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["plan_curvature"]   = {"wave-sine", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["tri"]              = {"chart-scatter", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["roughness"]        = {"chart-line-down", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["fill_sinks"]       = {"arrow-fat-down", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["flow_direction"]   = {"arrow-bend-down-right", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["flow_accumulation"] = {"git-merge", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["stream_extract"]   = {"waves", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["watershed"]        = {"tree-structure", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["profile_extract"]  = {"chart-line-up", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["viewshed"]         = {"eye", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["viewshed_multi"]   = {"eyes", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["cut_fill"]         = {"arrows-vertical", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["reservoir_volume"] = {"drop-half-bottom", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["feature_stats"]          = {"chart-pie-slice", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["svm_classify"]           = {"brain", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["random_forest_classify"] = {"tree", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["max_likelihood_classify"] = {"chart-bar", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["ndvi"]         = {"plant", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["ndmi"]         = {"drop-half", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["evi"]          = {"leaf", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["evi2"]         = {"leaf", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["savi"]         = {"plant", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["osavi"]        = {"plant", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["gndvi"]        = {"plant", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["ndwi"]         = {"drop", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["mndwi"]        = {"drop-half", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["ndbi"]         = {"city", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["bsi"]          = {"mountains", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["arvi"]         = {"leaf", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["nbr"]          = {"fire", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["awei"]         = {"drop-half-bottom", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["ui"]           = {"city", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["bi"]           = {"fire", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["custom_index"] = {"function", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["projection:info"]       = {"info", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["raster_inspect:info"]   = {"info", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["vector:info"]           = {"info", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["vector:filter"]         = {"funnel", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["vector:clip"]           = {"scissors", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["terrain:watershed"]     = {"tree-structure", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["processing:watershed"]  = {"drop", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["processing:filter"]     = {"funnel", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["processing:contour"]    = {"circle", IconWeight::Regular, QColor("#9AA8B8")};

    actionMap_["buffer"]                  = {"circle", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["rasterize"]               = {"grid-four", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["polygonize"]              = {"polygon", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["convert"]                 = {"arrows-left-right", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["union"]                   = {"circles-three-plus", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["difference"]              = {"minus-circle", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["intersect"]               = {"intersect", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["dissolve"]                = {"merge", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["simplify"]                = {"minus", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["repair"]                  = {"wrench", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["geom_metrics"]            = {"ruler", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["nearest"]                 = {"arrows-out", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["spatial_join"]            = {"link", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["adjacency"]               = {"graph", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["overlap_check"]           = {"copy", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["topology_check"]          = {"check-circle", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["convex_hull"]             = {"hexagon", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["centroid"]                = {"dot", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["envelope"]                = {"square", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["boundary"]                = {"square-half", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["multipart_check"]         = {"circles-three", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["singlepart"]              = {"circle", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["vertices_extract"]        = {"dots-three", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["endpoints_extract"]       = {"arrow-line-up", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["midpoints_extract"]       = {"dot-outline", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["interior_point"]          = {"dot", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["duplicate_point_check"]   = {"copy", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["hole_check"]              = {"circle-dashed", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["dangling_endpoint_check"] = {"arrow-bend-up-right", IconWeight::Regular, QColor("#9AA8B8")};
    actionMap_["sliver_remove"]           = {"eraser", IconWeight::Regular, QColor("#9AA8B8")};

    cardMap_["input"]    = {"arrow-down", IconWeight::Regular, QColor("#2F7CF6")};
    cardMap_["output"]   = {"arrow-up", IconWeight::Regular, QColor("#2F7CF6")};
    cardMap_["advanced"] = {"sliders", IconWeight::Regular, QColor("#2F7CF6")};
}

QIcon IconManager::iconForPlugin(const std::string& pluginName, const QColor& color) {
    return QIcon(pixmapForPlugin(pluginName, 18, color));
}

QIcon IconManager::iconForAction(const std::string& actionKey, const QColor& color) {
    return QIcon(pixmapForAction(actionKey, 16, color));
}

QIcon IconManager::iconForCard(const std::string& cardType, const QColor& color) {
    return QIcon(pixmapForCard(cardType, 16, color));
}

QPixmap IconManager::pixmapForPlugin(const std::string& pluginName, int size, const QColor& color) {
    auto it = pluginMap_.find(pluginName);
    if (it == pluginMap_.end()) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(color, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(2, 2, size - 4, size - 4));
        return pixmap;
    }
    const auto& spec = it->second;
    const std::string key = cacheKey(spec.svgName, spec.weight, size, color);
    auto cacheIt = cache_.find(key);
    if (cacheIt != cache_.end()) {
        return cacheIt->second;
    }
    const std::string path = svgPathFor(spec.svgName, spec.weight);
    QPixmap rendered = renderSvg(path, size, color);
    cache_[key] = rendered;
    return rendered;
}

QPixmap IconManager::pixmapForAction(const std::string& actionKey, int size, const QColor& color) {
    auto it = actionMap_.find(actionKey);
    if (it == actionMap_.end()) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(color, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(3, 3, size - 6, size - 6));
        return pixmap;
    }
    const auto& spec = it->second;
    const std::string key = cacheKey(spec.svgName, spec.weight, size, color);
    auto cacheIt = cache_.find(key);
    if (cacheIt != cache_.end()) {
        return cacheIt->second;
    }
    const std::string path = svgPathFor(spec.svgName, spec.weight);
    QPixmap rendered = renderSvg(path, size, color);
    cache_[key] = rendered;
    return rendered;
}

QPixmap IconManager::pixmapForAction(const std::string& pluginName, const std::string& actionKey,
                                     int size, const QColor& color) {
    const std::string compositeKey = pluginName + ":" + actionKey;
    auto it = actionMap_.find(compositeKey);
    if (it == actionMap_.end()) {
        it = actionMap_.find(actionKey);
    }
    if (it == actionMap_.end()) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(color, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(3, 3, size - 6, size - 6));
        return pixmap;
    }
    const auto& spec = it->second;
    const std::string key = cacheKey(spec.svgName, spec.weight, size, color);
    auto cacheIt = cache_.find(key);
    if (cacheIt != cache_.end()) {
        return cacheIt->second;
    }
    const std::string path = svgPathFor(spec.svgName, spec.weight);
    QPixmap rendered = renderSvg(path, size, color);
    cache_[key] = rendered;
    return rendered;
}

QPixmap IconManager::pixmapForCard(const std::string& cardType, int size, const QColor& color) {
    auto it = cardMap_.find(cardType);
    if (it == cardMap_.end()) {
        return pixmapForAction("info", size, color);
    }
    const auto& spec = it->second;
    const std::string key = cacheKey(spec.svgName, spec.weight, size, color);
    auto cacheIt = cache_.find(key);
    if (cacheIt != cache_.end()) {
        return cacheIt->second;
    }
    const std::string path = svgPathFor(spec.svgName, spec.weight);
    QPixmap rendered = renderSvg(path, size, color);
    cache_[key] = rendered;
    return rendered;
}

bool IconManager::hasPluginIcon(const std::string& pluginName) const {
    return pluginMap_.find(pluginName) != pluginMap_.end();
}

bool IconManager::hasActionIcon(const std::string& actionKey) const {
    return actionMap_.find(actionKey) != actionMap_.end();
}

bool IconManager::hasActionIcon(const std::string& pluginName, const std::string& actionKey) const {
    const std::string compositeKey = pluginName + ":" + actionKey;
    return actionMap_.find(compositeKey) != actionMap_.end()
        || actionMap_.find(actionKey) != actionMap_.end();
}

std::string IconManager::svgPathFor(const std::string& svgName, IconWeight weight) const {
    const std::string weightDir = (weight == IconWeight::Bold) ? "bold" : "regular";
    const std::string suffix = (weight == IconWeight::Bold) ? "-bold" : "-regular";
    return basePath_ + "/" + weightDir + "/" + svgName + suffix + ".svg";
}

QPixmap IconManager::renderSvg(const std::string& svgPath, int size, const QColor& color) {
    QFile file(QString::fromStdString(svgPath));
    if (!file.open(QIODevice::ReadOnly)) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        return pixmap;
    }

    QString svgContent = QTextStream(&file).readAll();
    file.close();

    svgContent.replace("stroke=\"currentColor\"", QString("stroke=\"%1\"").arg(color.name()));
    svgContent.replace("fill=\"currentColor\"", QString("fill=\"%1\"").arg(color.name()));

    if (!svgContent.contains("color=\"")) {
        svgContent.replace("<svg", QString("<svg color=\"%1\" ").arg(color.name()));
    }

    QSvgRenderer renderer(svgContent.toUtf8());
    if (!renderer.isValid()) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        return pixmap;
    }

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return pixmap;
}

} // namespace gis::gui

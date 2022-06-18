#include "LinePatternSymbolizer.h"

namespace carto::mvt {
    LinePatternSymbolizer::FeatureProcessor LinePatternSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::FloatFunction opacityFunc = _opacity.getFunction(exprContext);
        vt::ColorFunction colorFunc = _fill.getFunction(exprContext);
        if (opacityFunc == vt::FloatFunction(0) || colorFunc == vt::ColorFunction(vt::Color())) {
            return FeatureProcessor();
        }
        vt::FloatFunction offsetFunc = _offset.getFunction(exprContext);

        std::shared_ptr<const vt::BitmapPattern> bitmapPattern;
        std::string file = _file.getValue(exprContext);
        if (!file.empty()) {
            bitmapPattern = symbolizerContext.getBitmapManager()->loadBitmapPattern(file, PATTERN_SCALE, PATTERN_SCALE);
            if (!bitmapPattern || !bitmapPattern->bitmap) {
                _logger->write(Logger::Severity::ERROR, "Failed to load line pattern bitmap " + file);
                return FeatureProcessor();
            }
        } else {
            return FeatureProcessor();
        }

        vt::CompOp compOp = _compOp.getValue(exprContext);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);
        vt::FloatFunction widthFunc = _widthFuncBuilder.createFloatFunction(bitmapPattern->bitmap->height);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(colorFunc, opacityFunc);

        vt::LineStyle style(compOp, vt::LineJoinMode::MITER, vt::LineCapMode::NONE, fillFunc, widthFunc, offsetFunc, PATTERN_DOT_LIMIT, PATTERN_DOT_LIMIT, bitmapPattern, geometryTransform);

        std::shared_ptr<vt::StrokeMap> strokeMap = symbolizerContext.getStrokeMap();

        return [style, strokeMap, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            bool suppressWarning = false;
            if (auto lineProcessor = layerBuilder.createLineProcessor(style, strokeMap)) {
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (auto lineGeometry = std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertices : lineGeometry->getVerticesList()) {
                            lineProcessor(featureCollection.getLocalId(featureIndex), vertices);
                        }
                    }
                    else if (auto polygonGeometry = std::get_if<PolygonGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& verticesList : polygonGeometry->getPolygonList()) {
                            for (const auto& vertices : verticesList) {
                                lineProcessor(featureCollection.getLocalId(featureIndex), vertices);
                            }
                        }
                    }
                    else if (!suppressWarning) {
                        _logger->write(Logger::Severity::WARNING, "Unsupported geometry for LinePatternSymbolizer");
                        suppressWarning = true;
                    }
                }
            }
        };
    }
}

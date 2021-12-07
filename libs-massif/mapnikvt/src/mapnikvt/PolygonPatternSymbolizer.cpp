#include "PolygonPatternSymbolizer.h"

namespace carto { namespace mvt {
    PolygonPatternSymbolizer::FeatureProcessor PolygonPatternSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::FloatFunction opacityFunc = _opacity.getFunction(exprContext);
        vt::ColorFunction colorFunc = _fill.getFunction(exprContext);
        if (opacityFunc == vt::FloatFunction(0) || colorFunc == vt::ColorFunction(vt::Color())) {
            return FeatureProcessor();
        }

        std::shared_ptr<const vt::BitmapPattern> bitmapPattern;
        std::string file = _file.getValue(exprContext);
        if (!file.empty()) {
            bitmapPattern = symbolizerContext.getBitmapManager()->loadBitmapPattern(file, PATTERN_SCALE, PATTERN_SCALE);
            if (!bitmapPattern || !bitmapPattern->bitmap) {
                _logger->write(Logger::Severity::ERROR, "Failed to load polygon pattern bitmap " + file);
                return FeatureProcessor();
            }
        } else {
            return FeatureProcessor();
        }

        vt::CompOp compOp = _compOp.getValue(exprContext);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(colorFunc, opacityFunc);

        vt::PolygonStyle style(compOp, fillFunc, bitmapPattern, geometryTransform);

        return [style, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            bool suppressWarning = false;
            if (auto polygonProcessor = layerBuilder.createPolygonProcessor(style)) {
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (auto polygonGeometry = featureCollection.getPolygonGeometry(featureIndex)) {
                        for (const auto& verticesList : polygonGeometry->getPolygonList()) {
                            polygonProcessor(featureCollection.getLocalId(featureIndex), verticesList);
                        }
                    }
                    else if (!suppressWarning) {
                        _logger->write(Logger::Severity::WARNING, "Unsupported geometry for PolygonPatternSymbolizer");
                        suppressWarning = true;
                    }
                }
            }
        };
    }
} }

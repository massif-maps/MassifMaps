#include "PolygonPatternSymbolizer.h"

namespace carto { namespace mvt {
    void PolygonPatternSymbolizer::build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const {
        vt::FloatFunction opacityFunc = _opacity.getFunction(exprContext);
        vt::ColorFunction colorFunc = _fill.getFunction(exprContext);
        if (opacityFunc == vt::FloatFunction(0) || colorFunc == vt::ColorFunction(vt::Color())) {
            return;
        }

        std::string file = _file.getValue(exprContext);
        std::shared_ptr<const vt::BitmapPattern> bitmapPattern = symbolizerContext.getBitmapManager()->loadBitmapPattern(file, PATTERN_SCALE, PATTERN_SCALE);
        if (!bitmapPattern || !bitmapPattern->bitmap) {
            _logger->write(Logger::Severity::ERROR, "Failed to load polygon pattern bitmap " + file);
            return;
        }

        vt::CompOp compOp = _compOp.getValue(exprContext);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(colorFunc, opacityFunc);

        vt::PolygonStyle style(compOp, fillFunc, bitmapPattern, geometryTransform);

        std::size_t featureIndex = 0;
        std::size_t geometryIndex = 0;
        std::shared_ptr<const PolygonGeometry> polygonGeometry;
        layerBuilder.addPolygons([&](long long& id, vt::TileLayerBuilder::VerticesList& verticesList) {
            while (true) {
                if (polygonGeometry) {
                    if (geometryIndex < polygonGeometry->getPolygonList().size()) {
                        id = featureCollection.getLocalId(featureIndex);
                        verticesList = polygonGeometry->getPolygonList()[geometryIndex++];
                        return true;
                    }
                    featureIndex++;
                    geometryIndex = 0;
                }

                if (featureIndex >= featureCollection.size()) {
                    break;
                }
                polygonGeometry = featureCollection.getPolygonGeometry(featureIndex);
                if (!polygonGeometry) {
                    _logger->write(Logger::Severity::WARNING, "Unsupported geometry for PolygonPatternSymbolizer");
                    featureIndex++;
                }
            }
            return false;
        }, style);
    }
} }

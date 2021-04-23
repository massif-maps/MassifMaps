#include "LinePatternSymbolizer.h"

namespace carto { namespace mvt {
    void LinePatternSymbolizer::build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const {
        vt::FloatFunction opacityFunc = _opacity.getFunction(exprContext);
        vt::ColorFunction colorFunc = _fill.getFunction(exprContext);
        if (opacityFunc == vt::FloatFunction(0) || colorFunc == vt::ColorFunction(vt::Color())) {
            return;
        }
        
        std::string file = _file.getValue(exprContext);
        std::shared_ptr<const vt::BitmapPattern> bitmapPattern = symbolizerContext.getBitmapManager()->loadBitmapPattern(file, PATTERN_SCALE, PATTERN_SCALE);
        if (!bitmapPattern || !bitmapPattern->bitmap) {
            _logger->write(Logger::Severity::ERROR, "Failed to load line pattern bitmap " + file);
            return;
        }
        
        vt::CompOp compOp = _compOp.getValue(exprContext);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);
        vt::FloatFunction widthFunc = _widthFuncBuilder.createFloatFunction(bitmapPattern->bitmap->height);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(colorFunc, opacityFunc);

        vt::LineStyle style(compOp, vt::LineJoinMode::MITER, vt::LineCapMode::NONE, fillFunc, widthFunc, bitmapPattern, geometryTransform);

        std::size_t featureIndex = 0;
        std::size_t geometryIndex = 0;
        std::size_t polygonIndex = 0;
        std::shared_ptr<const LineGeometry> lineGeometry;
        std::shared_ptr<const PolygonGeometry> polygonGeometry;
        layerBuilder.addLines([&](long long& id, vt::TileLayerBuilder::Vertices& vertices) {
            while (true) {
                if (lineGeometry) {
                    if (geometryIndex < lineGeometry->getVerticesList().size()) {
                        id = featureCollection.getLocalId(featureIndex);
                        vertices = lineGeometry->getVerticesList()[geometryIndex++];
                        return true;
                    }
                    featureIndex++;
                    geometryIndex = 0;
                }
                if (polygonGeometry) {
                    while (geometryIndex < polygonGeometry->getPolygonList().size()) {
                        if (polygonIndex < polygonGeometry->getPolygonList()[geometryIndex].size()) {
                            id = featureCollection.getLocalId(featureIndex);
                            vertices = polygonGeometry->getPolygonList()[geometryIndex][polygonIndex++];
                            return true;
                        }
                        geometryIndex++;
                        polygonIndex = 0;
                    }
                    featureIndex++;
                    geometryIndex = 0;
                }

                if (featureIndex >= featureCollection.size()) {
                    break;
                }
                lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(featureCollection.getGeometry(featureIndex));
                polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(featureCollection.getGeometry(featureIndex));
                if (!lineGeometry && !polygonGeometry) {
                    _logger->write(Logger::Severity::WARNING, "Unsupported geometry for LinePatternSymbolizer");
                    featureIndex++;
                }
            }
            return false;
        }, style, symbolizerContext.getStrokeMap());
    }
} }

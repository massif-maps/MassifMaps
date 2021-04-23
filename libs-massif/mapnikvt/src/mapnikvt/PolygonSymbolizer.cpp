#include "PolygonSymbolizer.h"
#include "ParserUtils.h"

namespace carto { namespace mvt {
    void PolygonSymbolizer::build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const {
        vt::ColorFunction fillColorFunc = _fill.getFunction(exprContext);
        vt::FloatFunction fillOpacityFunc = _fillOpacity.getFunction(exprContext);
        if (fillOpacityFunc == vt::FloatFunction(0) || fillColorFunc == vt::ColorFunction(vt::Color())) {
            return;
        }

        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(fillColorFunc, fillOpacityFunc);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);

        vt::PolygonStyle style(compOp, fillFunc, std::shared_ptr<vt::BitmapPattern>(), geometryTransform);

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
                    _logger->write(Logger::Severity::WARNING, "Unsupported geometry for PolygonSymbolizer");
                    featureIndex++;
                }
            }
            return false;
        }, style);
    }
} }

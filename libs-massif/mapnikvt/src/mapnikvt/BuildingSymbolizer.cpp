#include "BuildingSymbolizer.h"
#include "ParserUtils.h"

#include <cmath>

namespace carto { namespace mvt {
    void BuildingSymbolizer::build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const {
        vt::FloatFunction fillOpacityFunc = _fillOpacity.getFunction(exprContext);
        vt::ColorFunction fillColorFunc = _fill.getFunction(exprContext);
        if (fillOpacityFunc == vt::FloatFunction(0) || fillColorFunc == vt::ColorFunction(vt::Color())) {
            return;
        }

        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(fillColorFunc, fillOpacityFunc);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);
        float height = _height.getValue(exprContext);
        float minHeight = _minHeight.getValue(exprContext);

        vt::Polygon3DStyle style(fillFunc, geometryTransform);

        std::size_t featureIndex = 0;
        std::size_t geometryIndex = 0;
        std::shared_ptr<const PolygonGeometry> polygonGeometry;
        layerBuilder.addPolygons3D([&](long long& id, vt::TileLayerBuilder::VerticesList& verticesList) {
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
                    _logger->write(Logger::Severity::WARNING, "Unsupported geometry for BuildingSymbolizer");
                    featureIndex++;
                }
            }
            return false;
        }, minHeight, height, style);
    }
} }

#include "BuildingSymbolizer.h"
#include "ParserUtils.h"

#include <cmath>

namespace carto::mvt {
    BuildingSymbolizer::FeatureProcessor BuildingSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::FloatFunction fillOpacityFunc = _fillOpacity.getFunction(exprContext);
        vt::ColorFunction fillColorFunc = _fill.getFunction(exprContext);
        if (fillOpacityFunc == vt::FloatFunction(0) || fillColorFunc == vt::ColorFunction(vt::Color())) {
            return FeatureProcessor();
        }

        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(fillColorFunc, fillOpacityFunc);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);
        float height = _height.getValue(exprContext);
        float minHeight = _minHeight.getValue(exprContext);

        vt::Polygon3DStyle style(fillFunc, geometryTransform);

        return [style, height, minHeight, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            bool suppressWarning = false;
            if (auto polygon3DProcessor = layerBuilder.createPolygon3DProcessor(style)) {
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (auto polygonGeometry = featureCollection.getPolygonGeometry(featureIndex)) {
                        for (const auto& verticesList : polygonGeometry->getPolygonList()) {
                            polygon3DProcessor(featureCollection.getLocalId(featureIndex), verticesList, minHeight, height);
                        }
                    }
                    else if (!suppressWarning) {
                        _logger->write(Logger::Severity::WARNING, "Unsupported geometry for BuildingSymbolizer");
                        suppressWarning = true;
                    }
                }
            }
        };
    }
}

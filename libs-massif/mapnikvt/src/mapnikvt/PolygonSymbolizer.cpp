#include "PolygonSymbolizer.h"
#include "ParserUtils.h"

namespace carto { namespace mvt {
    PolygonSymbolizer::FeatureProcessor PolygonSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::ColorFunction fillColorFunc = _fill.getFunction(exprContext);
        vt::FloatFunction fillOpacityFunc = _fillOpacity.getFunction(exprContext);
        if (fillOpacityFunc == vt::FloatFunction(0) || fillColorFunc == vt::ColorFunction(vt::Color())) {
            return FeatureProcessor();
        }

        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(fillColorFunc, fillOpacityFunc);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);

        vt::PolygonStyle style(compOp, fillFunc, std::shared_ptr<vt::BitmapPattern>(), geometryTransform);

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
                        _logger->write(Logger::Severity::WARNING, "Unsupported geometry for PolygonSymbolizer");
                        suppressWarning = true;
                    }
                }
            }
        };
    }
} }

#include "PolygonSymbolizer.h"
#include "ParserUtils.h"

namespace massif::mvt {
    PolygonSymbolizer::FeatureProcessor PolygonSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::ColorFunction fillColorFunc = _fill.getFunction(exprContext);
        vt::FloatFunction fillOpacityFunc = _fillOpacity.getFunction(exprContext);
        if (fillOpacityFunc == vt::FloatFunction(0) || fillColorFunc == vt::ColorFunction(vt::Color())) {
            return FeatureProcessor();
        }

        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(fillColorFunc, fillOpacityFunc);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);

        std::string elevationModeName = _elevationMode.getValue(exprContext);
        vt::LineElevationMode elevationMode = vt::LineElevationMode::DRAPE;
        if (elevationModeName == "span") {
            elevationMode = vt::LineElevationMode::SPAN;
        } else if (elevationModeName == "underground") {
            elevationMode = vt::LineElevationMode::UNDERGROUND;
        } else if (elevationModeName != "drape") {
            _logger->write(Logger::Severity::WARNING, "Unsupported elevation-mode: " + elevationModeName);
        }

        vt::PolygonStyle style(compOp, fillFunc, std::shared_ptr<vt::BitmapPattern>(), geometryTransform, _fillEmissive.getFunction(exprContext), elevationMode);

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
}

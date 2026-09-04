#include "BuildingSymbolizer.h"
#include "ParserUtils.h"

#include <cmath>

namespace massif::mvt {
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

        std::string roofShapeName = _roofShape.getValue(exprContext);
        vt::RoofShape roofShape = vt::RoofShape::FLAT;
        if (roofShapeName == "pyramidal") {
            roofShape = vt::RoofShape::PYRAMIDAL;
        }
        else if (roofShapeName == "gabled") {
            roofShape = vt::RoofShape::GABLED;
        }
        float roofHeight = _roofHeight.getValue(exprContext);

        std::string elevationModeName = _elevationMode.getValue(exprContext);
        vt::LineElevationMode elevationMode = vt::LineElevationMode::DRAPE;
        if (elevationModeName == "span") {
            elevationMode = vt::LineElevationMode::SPAN;
        } else if (elevationModeName == "underground") {
            elevationMode = vt::LineElevationMode::UNDERGROUND;
        } else if (elevationModeName != "drape") {
            _logger->write(Logger::Severity::WARNING, "Unsupported elevation-mode: " + elevationModeName);
        }

        // UNSET stays unset: the renderer then takes the Map block's building-emissive, so a rule
        // that says nothing renders exactly as it did before this property existed.
        vt::FloatFunction emissiveRaw = _emissive.getFunction(exprContext);
        std::optional<vt::FloatFunction> emissiveFunc;
        if (!(emissiveRaw == vt::FloatFunction(-1.0f))) {
            emissiveFunc = emissiveRaw;
        }

        vt::Polygon3DStyle style(fillFunc, geometryTransform, roofShape, roofHeight, elevationMode, emissiveFunc);

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

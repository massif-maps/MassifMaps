#include "LinePatternSymbolizer.h"

namespace carto { namespace mvt {
    void LinePatternSymbolizer::build(const FeatureCollection& featureCollection, const FeatureExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) {
        std::lock_guard<std::mutex> lock(_mutex);

        updateBindings(exprContext);

        if (_opacityFunc == vt::FloatFunction(0) || _fillFunc == vt::ColorFunction(vt::Color())) {
            return;
        }
        
        std::shared_ptr<const vt::BitmapPattern> bitmapPattern = symbolizerContext.getBitmapManager()->loadBitmapPattern(_file, PATTERN_SCALE, PATTERN_SCALE);
        if (!bitmapPattern || !bitmapPattern->bitmap) {
            _logger->write(Logger::Severity::ERROR, "Failed to load line pattern bitmap " + _file);
            return;
        }
        
        vt::CompOp compOp = convertCompOp(_compOp);

        vt::FloatFunction widthFunc = _functionBuilder.createFloatFunction(bitmapPattern->bitmap->height);
        vt::ColorFunction fillFunc = _functionBuilder.createColorOpacityFunction(_fillFunc, _opacityFunc);

        vt::LineStyle style(compOp, vt::LineJoinMode::MITER, vt::LineCapMode::NONE, fillFunc, widthFunc, bitmapPattern, _geometryTransform);

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

    void LinePatternSymbolizer::bindParameter(const std::string& name, const std::string& value) {
        if (name == "file") {
            bind(&_file, parseStringExpression(value));
        }
        else if (name == "fill") {
            bind(&_fillFunc, parseStringExpression(value), &LinePatternSymbolizer::convertColor);
        }
        else if (name == "opacity") {
            bind(&_opacityFunc, parseExpression(value));
        }
        else {
            GeometrySymbolizer::bindParameter(name, value);
        }
    }
} }

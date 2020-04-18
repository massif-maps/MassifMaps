#include "LineSymbolizer.h"
#include "ParserUtils.h"
#include "vt/BitmapCanvas.h"

#include <cmath>
#include <algorithm>

#include <boost/algorithm/string.hpp>

namespace carto { namespace mvt {
    void LineSymbolizer::build(const FeatureCollection& featureCollection, const FeatureExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) {
        std::lock_guard<std::mutex> lock(_mutex);

        updateBindings(exprContext);

        if (_strokeWidthFunc == vt::FloatFunction(0) || _strokeOpacityFunc == vt::FloatFunction(0) || _strokeFunc == vt::ColorFunction(vt::Color())) {
            return;
        }
        
        vt::LineJoinMode lineJoin = convertLineJoinMode(_strokeLinejoin);
        vt::LineCapMode lineCap = convertLineCapMode(_strokeLinecap);
        vt::CompOp compOp = convertCompOp(_compOp);
        
        std::shared_ptr<const vt::BitmapPattern> strokePattern;
        if (!_strokeDashArray.empty()) {
            int height = 1;
            if (lineCap != vt::LineCapMode::NONE) {
                height = static_cast<int>(ValueConverter<double>::convert(_strokeWidthExpression->evaluate(exprContext)));
            }
            std::string file = "__line_dasharray_" + boost::lexical_cast<std::string>(height) + "_" + _strokeLinecap + "_" + _strokeDashArray;
            strokePattern = symbolizerContext.getBitmapManager()->getBitmapPattern(file);
            if (!strokePattern) {
                std::vector<std::string> dashList;
                boost::split(dashList, _strokeDashArray, boost::is_any_of(","));
                std::vector<float> strokeDashArray;
                for (const std::string& dash : dashList) {
                    try {
                        strokeDashArray.push_back(boost::lexical_cast<float>(boost::trim_copy(dash)));
                    }
                    catch (const boost::bad_lexical_cast&) {
                        _logger->write(Logger::Severity::ERROR, "Illegal dash value");
                    }
                }
                if (strokeDashArray.empty()) {
                    strokeDashArray.push_back(1);
                }
                strokePattern = createDashBitmapPattern(strokeDashArray, height, lineCap);
                symbolizerContext.getBitmapManager()->storeBitmapPattern(file, strokePattern);
            }
        }

        vt::ColorFunction strokeFunc = _functionBuilder.createColorOpacityFunction(_strokeFunc, _strokeOpacityFunc);
        
        vt::LineStyle style(compOp, lineJoin, lineCap, strokeFunc, _strokeWidthFunc, strokePattern, _geometryTransform);

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
                    _logger->write(Logger::Severity::WARNING, "Unsupported geometry for LineSymbolizer");
                    featureIndex++;
                }
            }
            return false;
        }, style, symbolizerContext.getStrokeMap());
    }

    void LineSymbolizer::bindParameter(const std::string& name, const std::string& value) {
        if (name == "stroke") {
            bind(&_strokeFunc, parseStringExpression(value), &LineSymbolizer::convertColor);
        }
        else if (name == "stroke-width") {
            _strokeWidthExpression = parseExpression(value);
            bind(&_strokeWidthFunc, _strokeWidthExpression);
        }
        else if (name == "stroke-opacity") {
            bind(&_strokeOpacityFunc, parseExpression(value));
        }
        else if (name == "stroke-linejoin") {
            bind(&_strokeLinejoin, parseStringExpression(value));
        }
        else if (name == "stroke-linecap") {
            bind(&_strokeLinecap, parseStringExpression(value));
        }
        else if (name == "stroke-dasharray") {
            bind(&_strokeDashArray, parseStringExpression(value));
        }
        else {
            GeometrySymbolizer::bindParameter(name, value);
        }
    }

    vt::LineCapMode LineSymbolizer::convertLineCapMode(const std::string& lineCap) const {
        if (lineCap == "round") {
            return vt::LineCapMode::ROUND;
        }
        else if (lineCap == "square") {
            return vt::LineCapMode::SQUARE;
        }
        else if (lineCap == "butt") {
            return vt::LineCapMode::NONE;
        }
        _logger->write(Logger::Severity::ERROR, std::string("Unsupported line cap mode: ") + lineCap);
        return vt::LineCapMode::NONE;
    }

    vt::LineJoinMode LineSymbolizer::convertLineJoinMode(const std::string& lineJoin) const {
        if (_strokeLinejoin == "round") {
            return vt::LineJoinMode::ROUND;
        }
        else if (_strokeLinejoin == "bevel") {
            return vt::LineJoinMode::BEVEL;
        }
        else if (_strokeLinejoin == "miter") {
            return vt::LineJoinMode::MITER;
        }
        _logger->write(Logger::Severity::ERROR, std::string("Unsupported line join mode: ") + lineJoin);
        return vt::LineJoinMode::MITER;
    }

    std::shared_ptr<vt::BitmapPattern> LineSymbolizer::createDashBitmapPattern(const std::vector<float>& strokeDashArray, int height, vt::LineCapMode lineCap) {
        float size = std::accumulate(strokeDashArray.begin(), strokeDashArray.end(), 0.0f);
        float superSamplingFactor = DASH_SUPERSAMPLING_FACTOR / std::accumulate(strokeDashArray.begin(), strokeDashArray.end(), 1.0f, [](float a, float b) { return b > 0 ? std::min(a, b) : a; });
        
        int pow2Size = 1;
        while (pow2Size < size * superSamplingFactor && pow2Size < 2048) {
            pow2Size *= 2;
        }
        float sizeScale = pow2Size / size;

        int pow2Height = 1;
        while (pow2Height < height * superSamplingFactor && pow2Height < 2048) {
            pow2Height *= 2;
        }
        float heightScale = pow2Height / height;

        vt::BitmapCanvas canvas(pow2Size, pow2Height, false);
        float radius = pow2Height * 0.5f * sizeScale / heightScale;
        float x0 = strokeDashArray.back() * 0.5f * sizeScale;
        float x1 = x0;
        float y0 = 0;
        float y1 = pow2Height;
        for (std::size_t n = 0; n < strokeDashArray.size(); n++) {
            x1 += strokeDashArray[n] * sizeScale;
            if (n % 2 == 0) {
                switch (lineCap) {
                case vt::LineCapMode::ROUND:
                    canvas.drawEllipse(x0, (y0 + y1) * 0.5f, radius, pow2Height * 0.5f);
                    canvas.drawEllipse(x1, (y0 + y1) * 0.5f, radius, pow2Height * 0.5f);
                    canvas.drawRectangle(x0, y0, x1, y1);
                    break;
                case vt::LineCapMode::SQUARE:
                    canvas.drawRectangle(x0 - radius, y0, x1 + radius, y1);
                    break;
                default:
                    canvas.drawRectangle(x0, y0, x1, y1);
                    break;
                }
            }
            x0 = x1;
        }
        
        return std::make_shared<vt::BitmapPattern>(DASH_PATTERN_SCALE / sizeScale, 1.0f / heightScale, canvas.buildBitmapImage()->bitmap);
    }
} }

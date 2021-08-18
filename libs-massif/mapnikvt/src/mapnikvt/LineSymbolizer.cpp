#include "LineSymbolizer.h"
#include "ParserUtils.h"
#include "vt/BitmapCanvas.h"

#include <cmath>
#include <algorithm>

#include <boost/algorithm/string.hpp>

namespace carto { namespace mvt {
    LineSymbolizer::FeatureProcessor LineSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::FloatFunction strokeWidthFunc = _strokeWidth.getFunction(exprContext);
        vt::FloatFunction strokeOpacityFunc = _strokeOpacity.getFunction(exprContext);
        vt::ColorFunction strokeColorFunc = _stroke.getFunction(exprContext);
        if (strokeWidthFunc == vt::FloatFunction(0) || strokeOpacityFunc == vt::FloatFunction(0) || strokeColorFunc == vt::ColorFunction(vt::Color())) {
            return FeatureProcessor();
        }
        vt::FloatFunction offsetFunc = _offset.getFunction(exprContext);

        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::LineJoinMode strokeLinejoin = _strokeLinejoin.getValue(exprContext);
        vt::LineCapMode strokeLinecap = _strokeLinecap.getValue(exprContext);
        vt::ColorFunction strokeFunc = _strokeFuncBuilder.createColorOpacityFunction(strokeColorFunc, strokeOpacityFunc);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);

        std::shared_ptr<const vt::BitmapPattern> strokePattern;
        std::string strokeDashArray = _strokeDashArray.getValue(exprContext);
        if (!strokeDashArray.empty()) {
            int height = 1;
            if (strokeLinecap != vt::LineCapMode::NONE) {
                height = _strokeWidth.getStaticValue(exprContext);
            }
            std::string file = "__line_dasharray_" + std::to_string(height) + "_" + std::to_string(static_cast<int>(strokeLinecap)) + "_" + strokeDashArray;
            strokePattern = symbolizerContext.getBitmapManager()->getBitmapPattern(file);
            if (!strokePattern) {
                std::vector<std::string> dashList;
                boost::split(dashList, strokeDashArray, boost::is_any_of(","));
                std::vector<float> strokeDashArray;
                for (const std::string& dash : dashList) {
                    try {
                        strokeDashArray.push_back(std::stof(boost::trim_copy(dash)));
                    }
                    catch (const boost::bad_lexical_cast&) {
                        _logger->write(Logger::Severity::ERROR, "Illegal dash value");
                    }
                }
                strokePattern = createDashBitmapPattern(strokeDashArray, height, strokeLinecap);
                symbolizerContext.getBitmapManager()->storeBitmapPattern(file, strokePattern);
            }
        }

        vt::LineStyle style(compOp, strokeLinejoin, strokeLinecap, strokeFunc, strokeWidthFunc, offsetFunc, strokePattern, geometryTransform);
        
        std::shared_ptr<vt::StrokeMap> strokeMap = symbolizerContext.getStrokeMap();

        return [style, strokeMap, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            bool suppressWarning = false;
            if (auto lineProcessor = layerBuilder.createLineProcessor(style, strokeMap)) {
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(featureCollection.getGeometry(featureIndex))) {
                        for (const auto& vertices : lineGeometry->getVerticesList()) {
                            lineProcessor(featureCollection.getLocalId(featureIndex), vertices);
                        }
                    }
                    else if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(featureCollection.getGeometry(featureIndex))) {
                        for (const auto& verticesList : polygonGeometry->getPolygonList()) {
                            for (const auto& vertices : verticesList) {
                                lineProcessor(featureCollection.getLocalId(featureIndex), vertices);
                            }
                        }
                    }
                    else if (!suppressWarning) {
                        _logger->write(Logger::Severity::WARNING, "Unsupported geometry for LineSymbolizer");
                        suppressWarning = true;
                    }
                }
            }
        };
    }

    std::shared_ptr<vt::BitmapPattern> LineSymbolizer::createDashBitmapPattern(const std::vector<float>& strokeDashArray, int height, vt::LineCapMode lineCap) {
        float size = std::accumulate(strokeDashArray.begin(), strokeDashArray.end(), 0.0f);
        if (size <= 0 || height <= 0) {
            return std::shared_ptr<vt::BitmapPattern>();
        }
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
                    canvas.drawEllipse(cglib::vec2<float>(x0, (y0 + y1) * 0.5f), radius, pow2Height * 0.5f);
                    canvas.drawEllipse(cglib::vec2<float>(x1, (y0 + y1) * 0.5f), radius, pow2Height * 0.5f);
                    canvas.drawRectangle(cglib::vec2<float>(x0, y0), cglib::vec2<float>(x1, y1));
                    break;
                case vt::LineCapMode::SQUARE:
                    canvas.drawRectangle(cglib::vec2<float>(x0 - radius, y0), cglib::vec2<float>(x1 + radius, y1));
                    break;
                default:
                    canvas.drawRectangle(cglib::vec2<float>(x0, y0), cglib::vec2<float>(x1, y1));
                    break;
                }
            }
            x0 = x1;
        }
        
        return std::make_shared<vt::BitmapPattern>(DASH_PATTERN_SCALE / sizeScale, 1.0f / heightScale, canvas.buildBitmapImage()->bitmap);
    }
} }

#include "TorqueMarkerSymbolizer.h"
#include "ParserUtils.h"
#include "vt/BitmapCanvas.h"

namespace carto { namespace mvt {
    void TorqueMarkerSymbolizer::build(const FeatureCollection& featureCollection, const FeatureExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) {
        std::lock_guard<std::mutex> lock(_mutex);

        updateBindings(exprContext);

        float width = DEFAULT_MARKER_SIZE, height = DEFAULT_MARKER_SIZE;
        if (_width > 0) {
            width = height = _width;
        }

        float bitmapScaleX = 1, bitmapScaleY = 1;
        float fillOpacity = _fillOpacity;
        std::shared_ptr<const vt::BitmapImage> bitmapImage;
        if (!_file.empty()) {
            bitmapImage = symbolizerContext.getBitmapManager()->loadBitmapImage(_file, false, 1.0f);
            if (!bitmapImage || !bitmapImage->bitmap) {
                _logger->write(Logger::Severity::ERROR, "Failed to load marker bitmap " + _file);
                return;
            }
            if (bitmapImage->bitmap->width < 1 || bitmapImage->bitmap->height < 1) {
                return;
            }

            width = bitmapImage->bitmap->width;
            height = bitmapImage->bitmap->height;
        }
        else {
            vt::Color fill = vt::Color::fromColorOpacity(_fill, _fillOpacity);
            vt::Color stroke = vt::Color::fromColorOpacity(_stroke, _strokeOpacity);
            if (_markerType == "rectangle") {
                std::string file = "__torque_marker_rectangle_" + std::to_string(width) + "_" + std::to_string(height) + "_" + std::to_string(fill.value()) + "_" + std::to_string(_strokeWidth) + "_" + std::to_string(stroke.value()) + ".bmp";
                bitmapImage = symbolizerContext.getBitmapManager()->getBitmapImage(file);
                if (!bitmapImage) {
                    bitmapImage = makeRectangleBitmap(width * SUPERSAMPLING_FACTOR, height * SUPERSAMPLING_FACTOR, fill, _strokeWidth * SUPERSAMPLING_FACTOR, stroke);
                    symbolizerContext.getBitmapManager()->storeBitmapImage(file, bitmapImage);
                }
            }
            else {
                std::string file = "__torque_marker_ellipse_" + std::to_string(width) + "_" + std::to_string(height) + "_" + std::to_string(fill.value()) + "_" + std::to_string(_strokeWidth) + "_" + std::to_string(stroke.value()) + ".bmp";
                bitmapImage = symbolizerContext.getBitmapManager()->getBitmapImage(file);
                if (!bitmapImage) {
                    bitmapImage = makeEllipseBitmap(width * SUPERSAMPLING_FACTOR, height * SUPERSAMPLING_FACTOR, fill, _strokeWidth * SUPERSAMPLING_FACTOR, stroke);
                    symbolizerContext.getBitmapManager()->storeBitmapImage(file, bitmapImage);
                }
            }
            bitmapScaleX = static_cast<float>(width) / bitmapImage->bitmap->width;
            bitmapScaleY = static_cast<float>(height) / bitmapImage->bitmap->height;
            fillOpacity = 1.0f;
        }

        float widthScale = bitmapScaleX * bitmapImage->scale;
        float heightScale = bitmapScaleY * bitmapImage->scale;
        vt::FloatFunction normalizedSizeFunc = _functionBuilder.createFloatFunction(widthScale);
        vt::ColorFunction fillFunc = _functionBuilder.createColorFunction(vt::Color::fromColorOpacity(vt::Color(1, 1, 1, 1), fillOpacity));

        std::optional<vt::Transform> optTransform;
        if (heightScale != widthScale) {
            optTransform = vt::Transform::fromMatrix2(cglib::scale2_matrix(cglib::vec2<float>(1.0f, heightScale / widthScale)));
        }
        vt::PointStyle style(getCompOp(), fillFunc, normalizedSizeFunc, bitmapImage, optTransform);

        std::size_t featureIndex = 0;
        std::size_t geometryIndex = 0;
        std::shared_ptr<const PointGeometry> pointGeometry;
        layerBuilder.addPoints([&](long long& id, vt::TileLayerBuilder::Vertex& vertex) {
            while (true) {
                if (pointGeometry) {
                    if (geometryIndex < pointGeometry->getVertices().size()) {
                        id = featureCollection.getLocalId(featureIndex);
                        vertex = pointGeometry->getVertices()[geometryIndex++];
                        return true;
                    }
                    featureIndex++;
                    geometryIndex = 0;
                }

                if (featureIndex >= featureCollection.size()) {
                    break;
                }
                pointGeometry = std::dynamic_pointer_cast<const PointGeometry>(featureCollection.getGeometry(featureIndex));
                if (!pointGeometry) {
                    _logger->write(Logger::Severity::WARNING, "Unsupported geometry for TorqueMarkerSymbolizer");
                }
            }
            return false;
        }, style, symbolizerContext.getGlyphMap());
    }

    void TorqueMarkerSymbolizer::bindParameter(const std::string& name, const std::string& value) {
        if (name == "file") {
            bind(&_file, parseStringExpression(value));
        }
        else if (name == "marker-type") {
            bind(&_markerType, parseStringExpression(value));
        }
        else if (name == "fill") {
            bind(&_fill, parseStringExpression(value), &TorqueMarkerSymbolizer::convertColor);
        }
        else if (name == "fill-opacity") {
            bind(&_fillOpacity, parseExpression(value));
        }
        else if (name == "width") {
            bind(&_width, parseExpression(value));
        }
        else if (name == "stroke") {
            bind(&_stroke, parseStringExpression(value), &TorqueMarkerSymbolizer::convertColor);
        }
        else if (name == "stroke-opacity") {
            bind(&_strokeOpacity, parseExpression(value));
        }
        else if (name == "stroke-width") {
            bind(&_strokeWidth, parseExpression(value));
        }
        else if (name == "opacity") { // binds to 2 parameters
            bind(&_fillOpacity, parseExpression(value));
            bind(&_strokeOpacity, parseExpression(value));
        }
        else {
            Symbolizer::bindParameter(name, value);
        }
    }

    std::shared_ptr<vt::BitmapImage> TorqueMarkerSymbolizer::makeEllipseBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor) {
        int canvasWidth = static_cast<int>(std::ceil(width + strokeWidth));
        int canvasHeight = static_cast<int>(std::ceil(height + strokeWidth));
        vt::BitmapCanvas canvas(canvasWidth, canvasHeight, false);
        float x0 = canvasWidth * 0.5f, y0 = canvasHeight * 0.5f;
        if (strokeWidth > 0) {
            canvas.setColor(strokeColor);
            canvas.drawEllipse(x0, y0, (width + strokeWidth * 0.5f) * 0.5f, (height + strokeWidth * 0.5f) * 0.5f);
        }
        canvas.setColor(color);
        canvas.drawEllipse(x0, y0, (width - strokeWidth * 0.5f) * 0.5f, (height - strokeWidth * 0.5f) * 0.5f);
        return canvas.buildBitmapImage();
    }

    std::shared_ptr<vt::BitmapImage> TorqueMarkerSymbolizer::makeRectangleBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor) {
        int canvasWidth = static_cast<int>(std::ceil(width + strokeWidth));
        int canvasHeight = static_cast<int>(std::ceil(height + strokeWidth));
        vt::BitmapCanvas canvas(canvasWidth, canvasHeight, false);
        if (strokeWidth > 0) {
            canvas.setColor(strokeColor);
            canvas.drawRectangle(0, 0, width + strokeWidth * 0.5f, height + strokeWidth * 0.5f);
        }
        canvas.setColor(color);
        canvas.drawRectangle(strokeWidth, strokeWidth, width - strokeWidth * 0.5f, height - strokeWidth * 0.5f);
        return canvas.buildBitmapImage();
    }
} }

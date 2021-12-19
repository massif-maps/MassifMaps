#include "TorqueMarkerSymbolizer.h"
#include "ParserUtils.h"
#include "vt/BitmapCanvas.h"

namespace carto { namespace mvt {
    TorqueMarkerSymbolizer::FeatureProcessor TorqueMarkerSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        float width = _width.getValue(exprContext);
        if (width <= 0) {
            width = DEFAULT_MARKER_SIZE;
        }
        float height = width;
        float fillOpacity = _fillOpacity.isDefined() ? _fillOpacity.getValue(exprContext) : _opacity.getValue(exprContext);
        float strokeOpacity = _strokeOpacity.isDefined() ? _strokeOpacity.getValue(exprContext) : _opacity.getValue(exprContext);

        float bitmapScaleX = 1, bitmapScaleY = 1;
        std::shared_ptr<const vt::BitmapImage> bitmapImage;
        std::string file = _file.getValue(exprContext);
        if (!file.empty()) {
            bitmapImage = symbolizerContext.getBitmapManager()->loadBitmapImage(file, 1.0f);
            if (!bitmapImage || !bitmapImage->bitmap) {
                _logger->write(Logger::Severity::ERROR, "Failed to load marker bitmap " + file);
                return FeatureProcessor();
            }
            if (bitmapImage->bitmap->width < 1 || bitmapImage->bitmap->height < 1) {
                return FeatureProcessor();
            }

            width = bitmapImage->bitmap->width;
            height = bitmapImage->bitmap->height;
        }
        else {
            vt::Color fill = vt::Color::fromColorOpacity(_fill.getValue(exprContext), fillOpacity);
            vt::Color stroke = vt::Color::fromColorOpacity(_stroke.getValue(exprContext), strokeOpacity);
            float strokeWidth = _strokeWidth.getValue(exprContext);
            std::string markerType = _markerType.getValue(exprContext);
            if (markerType == "rectangle") {
                file = "__torque_marker_rectangle_" + std::to_string(width) + "_" + std::to_string(height) + "_" + std::to_string(fill.value()) + "_" + std::to_string(strokeWidth) + "_" + std::to_string(stroke.value()) + ".bmp";
                bitmapImage = symbolizerContext.getBitmapManager()->getBitmapImage(file);
                if (!bitmapImage) {
                    bitmapImage = makeRectangleBitmap(width * SUPERSAMPLING_FACTOR, height * SUPERSAMPLING_FACTOR, fill, strokeWidth * SUPERSAMPLING_FACTOR, stroke);
                    symbolizerContext.getBitmapManager()->storeBitmapImage(file, bitmapImage);
                }
            }
            else {
                file = "__torque_marker_ellipse_" + std::to_string(width) + "_" + std::to_string(height) + "_" + std::to_string(fill.value()) + "_" + std::to_string(strokeWidth) + "_" + std::to_string(stroke.value()) + ".bmp";
                bitmapImage = symbolizerContext.getBitmapManager()->getBitmapImage(file);
                if (!bitmapImage) {
                    bitmapImage = makeEllipseBitmap(width * SUPERSAMPLING_FACTOR, height * SUPERSAMPLING_FACTOR, fill, strokeWidth * SUPERSAMPLING_FACTOR, stroke);
                    symbolizerContext.getBitmapManager()->storeBitmapImage(file, bitmapImage);
                }
            }
            bitmapScaleX = static_cast<float>(width) / bitmapImage->bitmap->width;
            bitmapScaleY = static_cast<float>(height) / bitmapImage->bitmap->height;
            fillOpacity = 1.0f;
        }

        float widthScale = bitmapScaleX * bitmapImage->scale;
        float heightScale = bitmapScaleY * bitmapImage->scale;
        vt::FloatFunction normalizedSizeFunc = _sizeFuncBuilder.createFloatFunction(widthScale);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorFunction(vt::Color::fromColorOpacity(vt::Color(1, 1, 1, 1), fillOpacity));

        std::optional<vt::Transform> transform;
        if (heightScale != widthScale) {
            transform = vt::Transform::fromMatrix2(cglib::scale2_matrix(cglib::vec2<float>(1.0f, heightScale / widthScale)));
        }
        vt::CompOp compOp = _compOp.getValue(exprContext);
        
        vt::PointStyle style(compOp, fillFunc, normalizedSizeFunc, bitmapImage, transform);

        std::shared_ptr<vt::GlyphMap> glyphMap = symbolizerContext.getGlyphMap();

        return [style, glyphMap, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            bool suppressWarning = false;
            if (auto pointProcessor = layerBuilder.createPointProcessor(style, glyphMap)) {
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const vt::TileLayerBuilder::Vertex& vertex : pointGeometry->getVertices()) {
                            pointProcessor(featureCollection.getLocalId(featureIndex), vertex);
                        }
                    }
                    else if (!suppressWarning) {
                        _logger->write(Logger::Severity::WARNING, "Unsupported geometry for TorqueMarkerSymbolizer");
                        suppressWarning = true;
                    }
                }
            }
        };
    }

    std::shared_ptr<vt::BitmapImage> TorqueMarkerSymbolizer::makeEllipseBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor) {
        int canvasWidth = static_cast<int>(std::ceil(width + strokeWidth));
        int canvasHeight = static_cast<int>(std::ceil(height + strokeWidth));
        vt::BitmapCanvas canvas(canvasWidth, canvasHeight);
        float x0 = canvasWidth * 0.5f, y0 = canvasHeight * 0.5f;
        if (strokeWidth > 0) {
            canvas.setColor(strokeColor);
            canvas.drawEllipse(cglib::vec2<float>(x0, y0), (width + strokeWidth * 0.5f) * 0.5f, (height + strokeWidth * 0.5f) * 0.5f);
        }
        canvas.setColor(color);
        canvas.drawEllipse(cglib::vec2<float>(x0, y0), (width - strokeWidth * 0.5f) * 0.5f, (height - strokeWidth * 0.5f) * 0.5f);
        return canvas.buildBitmapImage();
    }

    std::shared_ptr<vt::BitmapImage> TorqueMarkerSymbolizer::makeRectangleBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor) {
        int canvasWidth = static_cast<int>(std::ceil(width + strokeWidth));
        int canvasHeight = static_cast<int>(std::ceil(height + strokeWidth));
        vt::BitmapCanvas canvas(canvasWidth, canvasHeight);
        if (strokeWidth > 0) {
            canvas.setColor(strokeColor);
            canvas.drawRectangle(cglib::vec2<float>(0, 0), cglib::vec2<float>(width + strokeWidth * 0.5f, height + strokeWidth * 0.5f));
        }
        canvas.setColor(color);
        canvas.drawRectangle(cglib::vec2<float>(strokeWidth, strokeWidth), cglib::vec2<float>(width - strokeWidth * 0.5f, height - strokeWidth * 0.5f));
        return canvas.buildBitmapImage();
    }
} }

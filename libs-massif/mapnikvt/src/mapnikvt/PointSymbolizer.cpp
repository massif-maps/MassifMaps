#include "PointSymbolizer.h"
#include "ParserUtils.h"
#include "vt/BitmapCanvas.h"

namespace carto { namespace mvt {
    PointSymbolizer::FeatureProcessor PointSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::FloatFunction opacityFunc = _opacity.getFunction(exprContext);
        if (opacityFunc == vt::FloatFunction(0)) {
            return FeatureProcessor();
        }

        float fontScale = symbolizerContext.getSettings().getFontScale();
        std::string file = _file.getValue(exprContext);
        std::shared_ptr<const vt::BitmapImage> bitmapImage;
        if (!file.empty()) {
            bitmapImage = symbolizerContext.getBitmapManager()->loadBitmapImage(file, false, 1.0f);
            if (!bitmapImage || !bitmapImage->bitmap) {
                _logger->write(Logger::Severity::ERROR, "Failed to load point bitmap " + file);
                return FeatureProcessor();
            }
            if (bitmapImage->bitmap->width < 1 || bitmapImage->bitmap->height < 1) {
                return FeatureProcessor();
            }
        }
        else {
            file = "__default_point.bmp";
            bitmapImage = symbolizerContext.getBitmapManager()->getBitmapImage(file);
            if (!bitmapImage) {
                bitmapImage = makeRectangleBitmap(RECTANGLE_SIZE);
                symbolizerContext.getBitmapManager()->storeBitmapImage(file, bitmapImage);
            }
        }

        vt::FloatFunction sizeFunc = _sizeFuncBuilder.createFloatFunction(fontScale * bitmapImage->scale);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(_fillFuncBuilder.createColorFunction(vt::Color(1, 1, 1, 1)), opacityFunc);

        vt::CompOp compOp = _compOp.getValue(exprContext);
        std::optional<vt::Transform> transform = _transform.getValue(exprContext);
        
        vt::PointStyle style(compOp, fillFunc, sizeFunc, bitmapImage, transform);

        std::shared_ptr<vt::GlyphMap> glyphMap = symbolizerContext.getGlyphMap();

        return [style, glyphMap, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            if (auto pointProcessor = layerBuilder.createPointProcessor(style, glyphMap)) {
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (auto pointGeometry = std::dynamic_pointer_cast<const PointGeometry>(featureCollection.getGeometry(featureIndex))) {
                        for (const vt::TileLayerBuilder::Vertex& vertex : pointGeometry->getVertices()) {
                            pointProcessor(featureCollection.getLocalId(featureIndex), vertex);
                        }
                    }
                    else if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(featureCollection.getGeometry(featureIndex))) {
                        for (const vt::TileLayerBuilder::Vertex& vertex : lineGeometry->getMidPoints()) {
                            pointProcessor(featureCollection.getLocalId(featureIndex), vertex);
                        }
                    }
                    else if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(featureCollection.getGeometry(featureIndex))) {
                        for (const vt::TileLayerBuilder::Vertex& vertex : polygonGeometry->getSurfacePoints()) {
                            pointProcessor(featureCollection.getLocalId(featureIndex), vertex);
                        }
                    }
                };
            }
        };
    }

    std::shared_ptr<vt::BitmapImage> PointSymbolizer::makeRectangleBitmap(float size) {
        int canvasSize = static_cast<int>(std::ceil(size));
        vt::BitmapCanvas canvas(canvasSize, canvasSize, false);
        canvas.setColor(vt::Color(0, 0, 0, 1));
        canvas.drawRectangle(cglib::vec2<float>(0, 0), cglib::vec2<float>(size, size));
        return canvas.buildBitmapImage();
    }
} }

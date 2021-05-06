#include "PointSymbolizer.h"
#include "ParserUtils.h"
#include "vt/BitmapCanvas.h"

namespace carto { namespace mvt {
    void PointSymbolizer::build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const {
        vt::FloatFunction opacityFunc = _opacity.getFunction(exprContext);
        if (opacityFunc == vt::FloatFunction(0)) {
            return;
        }
        
        float fontScale = symbolizerContext.getSettings().getFontScale();
        std::string file = _file.getValue(exprContext);
        std::shared_ptr<const vt::BitmapImage> bitmapImage;
        if (!file.empty()) {
            bitmapImage = symbolizerContext.getBitmapManager()->loadBitmapImage(file, false, 1.0f);
            if (!bitmapImage || !bitmapImage->bitmap) {
                _logger->write(Logger::Severity::ERROR, "Failed to load point bitmap " + file);
                return;
            }
            if (bitmapImage->bitmap->width < 1 || bitmapImage->bitmap->height < 1) {
                return;
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
        vt::PointStyle pointStyle(compOp, fillFunc, sizeFunc, bitmapImage, transform);

        std::vector<std::pair<long long, vt::TileLayerBuilder::Vertex>> pointInfos;
        for (std::size_t index = 0; index < featureCollection.size(); index++) {
            long long localId = featureCollection.getLocalId(index);
            if (auto pointGeometry = std::dynamic_pointer_cast<const PointGeometry>(featureCollection.getGeometry(index))) {
                for (const auto& vertex : pointGeometry->getVertices()) {
                    pointInfos.emplace_back(localId, vertex);
                }
            }
            else if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(featureCollection.getGeometry(index))) {
                for (const auto& vertex : lineGeometry->getMidPoints()) {
                    pointInfos.emplace_back(localId, vertex);
                }
            }
            else if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(featureCollection.getGeometry(index))) {
                for (const auto& vertex : polygonGeometry->getSurfacePoints()) {
                    pointInfos.emplace_back(localId, vertex);
                }
            }
            else {
                _logger->write(Logger::Severity::WARNING, "Unsupported geometry for PointSymbolizer");
            }
        }

        std::size_t pointInfoIndex = 0;
        layerBuilder.addPoints([&](long long& id, vt::TileLayerBuilder::Vertex& vertex) {
            if (pointInfoIndex >= pointInfos.size()) {
                return false;
            }
            id = pointInfos[pointInfoIndex].first;
            vertex = pointInfos[pointInfoIndex].second;
            pointInfoIndex++;
            return true;
        }, pointStyle, symbolizerContext.getGlyphMap());
    }

    std::shared_ptr<vt::BitmapImage> PointSymbolizer::makeRectangleBitmap(float size) {
        int canvasSize = static_cast<int>(std::ceil(size));
        vt::BitmapCanvas canvas(canvasSize, canvasSize, false);
        canvas.setColor(vt::Color(0, 0, 0, 1));
        canvas.drawRectangle(cglib::vec2<float>(0, 0), cglib::vec2<float>(size, size));
        return canvas.buildBitmapImage();
    }
} }

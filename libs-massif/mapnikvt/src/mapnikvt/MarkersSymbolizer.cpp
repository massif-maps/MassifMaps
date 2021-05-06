#include "MarkersSymbolizer.h"
#include "ParserUtils.h"
#include "StringUtils.h"
#include "vt/BitmapCanvas.h"

namespace carto { namespace mvt {
    void MarkersSymbolizer::build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const {
        if ((_width.isDefined() && _width.getFunction(exprContext) == vt::FloatFunction(0)) || (_height.isDefined() && _height.getFunction(exprContext) == vt::FloatFunction(0))) {
            return;
        }

        bool allowOverlap = _allowOverlap.getValue(exprContext);
        bool clip = _clip.isDefined() ? _clip.getValue(exprContext) : allowOverlap;

        float fontScale = symbolizerContext.getSettings().getFontScale();
        float spacing = _spacing.getValue(exprContext);
        float placementPriority = _placementPriority.getValue(exprContext);
        float fillOpacity = _fillOpacity.isDefined() ? _fillOpacity.getValue(exprContext) : _opacity.getValue(exprContext);
        float strokeOpacity = _strokeOpacity.isDefined() ? _strokeOpacity.getValue(exprContext) : _opacity.getValue(exprContext);
        float widthStatic = _width.getStaticValue(exprContext);
        float heightStatic = _height.getStaticValue(exprContext);
        float strokeWidthStatic = _strokeWidth.getStaticValue(exprContext);

        std::optional<vt::Transform> transform = _transform.getValue(exprContext);
        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::LabelOrientation placement = _placement.getValue(exprContext);
        vt::LabelOrientation orientation = placement;
        if (transform) { // if rotation transform is explicitly defined, use point orientation
            if ((*transform).matrix2()(0, 1) != 0 || (*transform).matrix2()(1, 0) != 0) {
                orientation = vt::LabelOrientation::POINT;
            }
        }
        if (placement == vt::LabelOrientation::LINE && spacing > 0) {
            orientation = vt::LabelOrientation::POINT; // we will apply custom rotation, thus use point orientation
        }

        vt::FloatFunction sizeFunc;
        float bitmapScaleX = fontScale, bitmapScaleY = fontScale;
        std::shared_ptr<const vt::BitmapImage> bitmapImage;
        std::string file = _file.getValue(exprContext);
        if (!file.empty()) {
            bitmapImage = symbolizerContext.getBitmapManager()->loadBitmapImage(file, false, IMAGE_UPSAMPLING_SCALE);
            if (!bitmapImage || !bitmapImage->bitmap) {
                _logger->write(Logger::Severity::ERROR, "Failed to load marker bitmap " + file);
                return;
            }
            if (bitmapImage->bitmap->width < 1 || bitmapImage->bitmap->height < 1) {
                return;
            }
            
            if (_width.isDefined() && widthStatic > 0) {
                if (_height.isDefined() && heightStatic > 0) {
                    bitmapScaleY *= heightStatic / widthStatic;
                }
                else {
                    bitmapScaleY *= static_cast<float>(bitmapImage->bitmap->height) / bitmapImage->bitmap->width;
                }
                sizeFunc = _width.getFunction(exprContext);
            }
            else if (_height.isDefined() && heightStatic > 0) {
                bitmapScaleX *= static_cast<float>(bitmapImage->bitmap->width) / bitmapImage->bitmap->height;
                sizeFunc = _height.getFunction(exprContext);
            }
            else {
                bitmapScaleY *= static_cast<float>(bitmapImage->bitmap->height) / bitmapImage->bitmap->width;
                sizeFunc = _sizeFuncBuilder.createFloatFunction(bitmapImage->bitmap->width * bitmapImage->scale);
            }
        }
        else {
            vt::Color fill = vt::Color::fromColorOpacity(_fill.getValue(exprContext), fillOpacity);
            vt::Color stroke = vt::Color::fromColorOpacity(_stroke.getValue(exprContext), strokeOpacity);
            std::string markerType = toLower(_markerType.getValue(exprContext));
            bool ellipse = markerType == "ellipse" || (markerType.empty() && placement != vt::LabelOrientation::LINE);
            float bitmapWidth = (ellipse ? DEFAULT_CIRCLE_SIZE : DEFAULT_ARROW_WIDTH), bitmapHeight = (ellipse ? DEFAULT_CIRCLE_SIZE : DEFAULT_ARROW_HEIGHT);
            if (_width.isDefined()) { // NOTE: special case, if accept all values
                bitmapHeight = (_height.isDefined() ? heightStatic : widthStatic * bitmapHeight / bitmapWidth);
                bitmapWidth = widthStatic;
                bitmapScaleY *= bitmapHeight / bitmapWidth;
                sizeFunc = _width.getFunction(exprContext);
            }
            else if (_height.isDefined()) { // NOTE: special case, accept all values
                bitmapWidth = heightStatic * bitmapWidth / bitmapHeight;
                bitmapHeight = heightStatic;
                bitmapScaleX *= bitmapWidth / bitmapHeight;
                sizeFunc = _height.getFunction(exprContext);
            }
            else {
                bitmapScaleY *= bitmapHeight / bitmapWidth;
                sizeFunc = _sizeFuncBuilder.createFloatFunction(bitmapWidth);
            }
            bitmapScaleX *= (strokeWidthStatic + bitmapWidth) / bitmapWidth;
            bitmapScaleY *= (strokeWidthStatic + bitmapHeight) / bitmapHeight;
            bitmapWidth = std::min(bitmapWidth, static_cast<float>(MAX_BITMAP_SIZE));
            bitmapHeight = std::min(bitmapHeight, static_cast<float>(MAX_BITMAP_SIZE));
            
            if (ellipse) {
                file = "__default_marker_ellipse_" + std::to_string(bitmapWidth) + "_" + std::to_string(bitmapHeight) + "_" + std::to_string(fill.value()) + "_" + std::to_string(strokeWidthStatic) + "_" + std::to_string(stroke.value()) + ".bmp";
                bitmapImage = symbolizerContext.getBitmapManager()->getBitmapImage(file);
                if (!bitmapImage) {
                    bitmapImage = makeEllipseBitmap(bitmapWidth * SUPERSAMPLING_FACTOR, bitmapHeight * SUPERSAMPLING_FACTOR, fill, std::abs(strokeWidthStatic) * SUPERSAMPLING_FACTOR, stroke);
                    symbolizerContext.getBitmapManager()->storeBitmapImage(file, bitmapImage);
                }
            }
            else {
                file = "__default_marker_arrow_" + std::to_string(bitmapWidth) + "_" + std::to_string(bitmapHeight) + "_" + std::to_string(fill.value()) + "_" + std::to_string(strokeWidthStatic) + "_" + std::to_string(stroke.value()) + ".bmp";
                bitmapImage = symbolizerContext.getBitmapManager()->getBitmapImage(file);
                if (!bitmapImage) {
                    bitmapImage = makeArrowBitmap(bitmapWidth * SUPERSAMPLING_FACTOR, bitmapHeight * SUPERSAMPLING_FACTOR, fill, std::abs(strokeWidthStatic) * SUPERSAMPLING_FACTOR, stroke);
                    symbolizerContext.getBitmapManager()->storeBitmapImage(file, bitmapImage);
                }
            }

            fillOpacity = 1.0f;
        }

        float bitmapSize = static_cast<float>(std::max(widthStatic * fontScale, heightStatic * fontScale));
        long long groupId = (allowOverlap ? -1 : 0);

        float widthScale = bitmapScaleX / bitmapImage->scale / bitmapImage->bitmap->width;
        float heightScale = bitmapScaleY / bitmapImage->scale / bitmapImage->bitmap->height;
        vt::FloatFunction normalizedSizeFunc = _sizeFuncBuilder.createScaledFloatFunction(sizeFunc, widthScale);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorFunction(vt::Color::fromColorOpacity(vt::Color(1, 1, 1, 1), fillOpacity));

        std::vector<std::pair<long long, vt::TileLayerBuilder::Vertex>> pointInfos;
        std::vector<std::pair<long long, vt::TileLayerBuilder::PointLabelInfo>> labelInfos;

        auto addPoint = [&](long long localId, long long globalId, const std::variant<vt::TileLayerBuilder::Vertex, vt::TileLayerBuilder::Vertices>& position) {
            if (clip) {
                if (auto vertex = std::get_if<vt::TileLayerBuilder::Vertex>(&position)) {
                    pointInfos.emplace_back(localId, *vertex);
                }
                else if (auto vertices = std::get_if<vt::TileLayerBuilder::Vertices>(&position)) {
                    if (!vertices->empty()) {
                        pointInfos.emplace_back(localId, vertices->front());
                    }
                }
            }
            else {
                labelInfos.emplace_back(localId, vt::TileLayerBuilder::PointLabelInfo(globalId * 3 + 0, groupId, position, placementPriority, 0));
            }
        };

        auto flushPoints = [&](std::optional<vt::Transform> transform) {
            if (heightScale != widthScale) {
                transform = (transform ? *transform : vt::Transform()) * vt::Transform::fromMatrix2(cglib::scale2_matrix(cglib::vec2<float>(1.0f, heightScale / widthScale)));
            }

            if (clip) {
                vt::PointStyle style(compOp, fillFunc, normalizedSizeFunc, bitmapImage, transform);

                std::size_t pointInfoIndex = 0;
                layerBuilder.addPoints([&](long long& id, vt::TileLayerBuilder::Vertex& vertex) {
                    if (pointInfoIndex >= pointInfos.size()) {
                        return false;
                    }
                    id = pointInfos[pointInfoIndex].first;
                    vertex = pointInfos[pointInfoIndex].second;
                    pointInfoIndex++;
                    return true;
                }, style, symbolizerContext.getGlyphMap());
                
                pointInfos.clear();
            }
            else {
                vt::PointLabelStyle style(orientation, fillFunc, normalizedSizeFunc, false, bitmapImage, transform);

                std::size_t labelInfoIndex = 0;
                layerBuilder.addPointLabels([&](long long& id, vt::TileLayerBuilder::PointLabelInfo& labelInfo) {
                    if (labelInfoIndex >= labelInfos.size()) {
                        return false;
                    }
                    id = labelInfos[labelInfoIndex].first;
                    labelInfo = std::move(labelInfos[labelInfoIndex].second);
                    labelInfoIndex++;
                    return true;
                }, style, symbolizerContext.getGlyphMap());
                
                labelInfos.clear();
            }
        };

        auto addLinePoints = [&](long long localId, long long globalId, const std::vector<cglib::vec2<float>>& vertices) {
            if (spacing <= 0) {
                addPoint(localId, globalId, vertices);
                return;
            }

            flushPoints(transform); // NOTE: we need to flush previous points at this point as we will recalculate transform, which is part of the style

            float linePos = 0;
            for (std::size_t i = 1; i < vertices.size(); i++) {
                const cglib::vec2<float>& v0 = vertices[i - 1];
                const cglib::vec2<float>& v1 = vertices[i];

                float lineLen = cglib::length(v1 - v0) * symbolizerContext.getSettings().getTileSize();
                if (i == 1) {
                    linePos = std::min(lineLen, spacing) * 0.5f;
                }
                while (linePos < lineLen) {
                    cglib::vec2<float> pos = v0 + (v1 - v0) * (linePos / lineLen);
                    if (std::min(pos(0), pos(1)) > 0.0f && std::max(pos(0), pos(1)) < 1.0f) {
                        addPoint(localId, generateId(), pos);

                        cglib::vec2<float> dir = cglib::unit(v1 - v0);
                        cglib::mat2x2<float> dirTransform {{ dir(0), -dir(1) }, { dir(1), dir(0) }};
                        flushPoints(vt::Transform::fromMatrix2(dirTransform) * (transform ? *transform : vt::Transform())); // NOTE: we should flush to be sure that the point will not get buffered
                    }

                    linePos += spacing + bitmapSize;
                }

                linePos -= lineLen;
            }
        };

        std::size_t hash = std::hash<std::string>()(file);
        for (std::size_t index = 0; index < featureCollection.size(); index++) {
            long long localId = featureCollection.getLocalId(index);
            long long globalId = combineId(featureCollection.getGlobalId(index), hash);
            if (_featureId.isDefined()) {
                globalId = convertId(_featureId.getValue(exprContext));
                if (!globalId) {
                    globalId = generateId();
                }
            }

            const std::shared_ptr<const Geometry>& geometry = featureCollection.getGeometry(index);
            if (auto pointGeometry = std::dynamic_pointer_cast<const PointGeometry>(geometry)) {
                for (const auto& vertex : pointGeometry->getVertices()) {
                    addPoint(localId, globalId, vertex);
                }
            }
            else if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(geometry)) {
                if (placement == vt::LabelOrientation::LINE) {
                    for (const auto& vertices : lineGeometry->getVerticesList()) {
                        addLinePoints(localId, globalId, vertices);
                    }
                }
                else {
                    for (const auto& vertex : lineGeometry->getMidPoints()) {
                        addPoint(localId, globalId, vertex);
                    }
                }
            }
            else if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(geometry)) {
                if (placement == vt::LabelOrientation::LINE) {
                    for (const auto& vertices : polygonGeometry->getClosedOuterRings(true)) {
                        addLinePoints(localId, globalId, vertices);
                    }                  
                }
                else {
                    for (const auto& vertex : polygonGeometry->getSurfacePoints()) {
                        addPoint(localId, globalId, vertex);
                    }
                }
            }
            else {
                _logger->write(Logger::Severity::WARNING, "Unsupported geometry for MarkersSymbolizer");
            }
        }

        flushPoints(transform);
    }
    
    std::shared_ptr<vt::BitmapImage> MarkersSymbolizer::makeEllipseBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor) {
        int canvasWidth = static_cast<int>(std::ceil(width + strokeWidth));
        int canvasHeight = static_cast<int>(std::ceil(height + strokeWidth));
        vt::BitmapCanvas canvas(canvasWidth, canvasHeight, false);
        float x0 = canvasWidth * 0.5f, y0 = canvasHeight * 0.5f;
        if (strokeWidth > 0) {
            canvas.setColor(strokeColor);
            canvas.drawEllipse(cglib::vec2<float>(x0, y0), (width + strokeWidth) * 0.5f, (height + strokeWidth) * 0.5f);
        }
        canvas.setColor(color);
        canvas.drawEllipse(cglib::vec2<float>(x0, y0), (width - strokeWidth) * 0.5f, (height - strokeWidth) * 0.5f);
        return canvas.buildBitmapImage();
    }

    std::shared_ptr<vt::BitmapImage> MarkersSymbolizer::makeArrowBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor) {
        int canvasWidth = static_cast<int>(std::ceil(width + strokeWidth));
        int canvasHeight = static_cast<int>(std::ceil(height + strokeWidth));
        float x0 = 0, x1 = std::ceil(canvasWidth - canvasHeight * 0.5f), y1 = canvasHeight * 1.0f / 3.0f, y2 = canvasHeight * 2.0f / 3.0f;
        vt::BitmapCanvas canvas(canvasWidth, canvasHeight, false);
        if (strokeWidth > 0) {
            canvas.setColor(strokeColor);
            canvas.drawRectangle(cglib::vec2<float>(x0, y1 - strokeWidth), cglib::vec2<float>(x1 - strokeWidth, y2 + strokeWidth));
            canvas.drawTriangle(cglib::vec2<float>(x1 - strokeWidth, 0), cglib::vec2<float>(x1 - strokeWidth, canvasHeight), cglib::vec2<float>(canvasWidth, canvasHeight * 0.5f));
        }
        canvas.setColor(color);
        canvas.drawRectangle(cglib::vec2<float>(x0 + strokeWidth, y1 + strokeWidth * 0.5f), cglib::vec2<float>(x1, y2 - strokeWidth * 0.5f));
        canvas.drawTriangle(cglib::vec2<float>(x1, strokeWidth * 2 * std::sqrt(2.0f)), cglib::vec2<float>(x1, canvasHeight - strokeWidth * 2 * std::sqrt(2.0f)), cglib::vec2<float>(canvasWidth - strokeWidth * 2, canvasHeight * 0.5f));
        return canvas.buildBitmapImage();
    }
} }

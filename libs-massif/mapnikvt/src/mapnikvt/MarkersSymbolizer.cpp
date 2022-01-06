#include "MarkersSymbolizer.h"
#include "ParserUtils.h"
#include "vt/BitmapCanvas.h"

namespace carto { namespace mvt {
    MarkersSymbolizer::FeatureProcessor MarkersSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::FloatFunction opacityFunc = _opacity.getFunction(exprContext);
        vt::ColorFunction colorFunc = _color.getFunction(exprContext);
        if (opacityFunc == vt::FloatFunction(0) || colorFunc == vt::ColorFunction(vt::Color())) {
            return FeatureProcessor();
        }
        if ((_width.isDefined() && _width.getFunction(exprContext) == vt::FloatFunction(0)) || (_height.isDefined() && _height.getFunction(exprContext) == vt::FloatFunction(0))) {
            return FeatureProcessor();
        }

        bool allowOverlap = _allowOverlap.getValue(exprContext);
        bool clip = _clip.isDefined() ? _clip.getValue(exprContext) : allowOverlap;

        float fontScale = symbolizerContext.getSettings().getFontScale();
        float spacing = _spacing.getValue(exprContext);
        float placementPriority = _placementPriority.getValue(exprContext);
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
            bitmapImage = symbolizerContext.getBitmapManager()->loadBitmapImage(file, IMAGE_UPSAMPLING_SCALE);
            if (!bitmapImage || !bitmapImage->bitmap) {
                _logger->write(Logger::Severity::ERROR, "Failed to load marker bitmap " + file);
                return FeatureProcessor();
            }
            if (bitmapImage->bitmap->width < 1 || bitmapImage->bitmap->height < 1) {
                return FeatureProcessor();
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
            vt::Color fill = vt::Color::fromColorOpacity(_fill.getValue(exprContext), _fillOpacity.getValue(exprContext));
            vt::Color stroke = vt::Color::fromColorOpacity(_stroke.getValue(exprContext), _strokeOpacity.getValue(exprContext));
            std::string markerType = _markerType.getValue(exprContext);
            bool ellipse = markerType == "ellipse" || (markerType.empty() && placement != vt::LabelOrientation::LINE);
            float bitmapWidth = (ellipse ? DEFAULT_CIRCLE_SIZE : DEFAULT_ARROW_WIDTH), bitmapHeight = (ellipse ? DEFAULT_CIRCLE_SIZE : DEFAULT_ARROW_HEIGHT);
            if (_width.isDefined()) { // NOTE: special case, accept all values
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
        }

        float tileSize = symbolizerContext.getSettings().getTileSize();
        float bitmapSize = static_cast<float>(std::max(widthStatic * fontScale, heightStatic * fontScale));
        long long groupId = (allowOverlap ? -1 : 0);

        float widthScale = bitmapScaleX / bitmapImage->scale / bitmapImage->bitmap->width;
        float heightScale = bitmapScaleY / bitmapImage->scale / bitmapImage->bitmap->height;
        vt::FloatFunction normalizedSizeFunc = _sizeFuncBuilder.createScaledFloatFunction(sizeFunc, widthScale);
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(colorFunc, opacityFunc);

        if (heightScale != widthScale) {
            transform = (transform ? *transform : vt::Transform()) * vt::Transform::fromMatrix2(cglib::scale2_matrix(cglib::vec2<float>(1.0f, heightScale / widthScale)));
        }

        std::shared_ptr<vt::GlyphMap> glyphMap = symbolizerContext.getGlyphMap();

        vt::TileId tileId = exprContext.getTileId();
        std::size_t hash = std::hash<std::string>()(file);

        std::optional<long long> globalIdOverride;
        if (_featureId.isDefined()) {
            globalIdOverride = convertId(_featureId.getValue(exprContext));
        }

        if (clip) {
            return [compOp, fillFunc, normalizedSizeFunc, bitmapImage, transform, placement, spacing, bitmapSize, tileSize, glyphMap, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
                vt::PointStyle style(compOp, fillFunc, normalizedSizeFunc, bitmapImage, transform);
                vt::TileLayerBuilder::PointProcessor pointProcessor;
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (!pointProcessor) {
                        pointProcessor = layerBuilder.createPointProcessor(style, glyphMap);
                        if (!pointProcessor) {
                            return;
                        }
                    }
                    
                    if (auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertex : pointGeometry->getVertices()) {
                            pointProcessor(featureCollection.getLocalId(featureIndex), vertex);
                        }
                    }
                    else if (placement != vt::LabelOrientation::LINE) {
                        vt::TileLayerBuilder::Vertices vertices;
                        if (auto lineGeometry = std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                            vertices = lineGeometry->getMidPoints();
                        }
                        else if (auto polygonGeometry = std::get_if<PolygonGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                            vertices = polygonGeometry->getSurfacePoints();
                        }
                        
                        for (const auto& vertex : vertices) {
                            pointProcessor(featureCollection.getLocalId(featureIndex), vertex);
                        }
                    }
                    else {
                        vt::TileLayerBuilder::VerticesList verticesList;
                        if (auto lineGeometry = std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                            verticesList = lineGeometry->getVerticesList();
                        }
                        else if (auto polygonGeometry = std::get_if<PolygonGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                            verticesList = polygonGeometry->getClosedOuterRings(true);
                        }
                        
                        for (const auto& vertices : verticesList) {
                            for (const auto& transformedPoints : generateTransformedPoints(vertices, spacing, bitmapSize, tileSize)) {
                                vt::PointStyle transformedStyle(compOp, fillFunc, normalizedSizeFunc, bitmapImage, transformedPoints.first * (transform ? *transform : vt::Transform()));
                                pointProcessor = layerBuilder.createPointProcessor(transformedStyle, glyphMap);
                                if (pointProcessor) {
                                    for (const auto& vertex : transformedPoints.second) {
                                        pointProcessor(featureCollection.getLocalId(featureIndex), vertex);
                                    }
                                    pointProcessor = vt::TileLayerBuilder::PointProcessor();
                                }
                            }
                        }
                    }
                }
            };
        }

        return [compOp, fillFunc, normalizedSizeFunc, bitmapImage, transform, orientation, placement, placementPriority, spacing, bitmapSize, tileId, tileSize, glyphMap, groupId, globalIdOverride, hash, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            vt::PointLabelStyle style(orientation, fillFunc, normalizedSizeFunc, false, bitmapImage, transform);
            vt::TileLayerBuilder::PointLabelProcessor pointProcessor;
            for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                if (!pointProcessor) {
                    pointProcessor = layerBuilder.createPointLabelProcessor(style, glyphMap);
                    if (!pointProcessor) {
                        return;
                    }
                }

                long long localId = featureCollection.getLocalId(featureIndex);
                long long globalId = combineId(featureCollection.getGlobalId(featureIndex), hash);
                if (globalIdOverride) {
                    globalId = *globalIdOverride;
                    if (!globalId) {
                        globalId = generateId();
                    }
                }
                globalId = globalId * 3 + 0;

                if (auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                    for (const auto& vertex : pointGeometry->getVertices()) {
                        pointProcessor(localId, globalId, groupId, vertex, placementPriority, 0);
                    }
                }
                else if (placement != vt::LabelOrientation::LINE) {
                    if (auto lineGeometry = std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertices : lineGeometry->getVerticesList()) {
                            pointProcessor(localId, globalId, groupId, vertices, placementPriority, 0);
                        }
                    }
                    else if (auto polygonGeometry = std::get_if<PolygonGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertex : polygonGeometry->getSurfacePoints()) {
                            pointProcessor(localId, globalId, groupId, vertex, placementPriority, 0);
                        }
                    }
                }
                else {
                    vt::TileLayerBuilder::VerticesList verticesList;
                    if (auto lineGeometry = std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        verticesList = lineGeometry->getVerticesList();
                    }
                    else if (auto polygonGeometry = std::get_if<PolygonGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        verticesList = polygonGeometry->getClosedOuterRings(true);
                    }

                    for (const auto& vertices : verticesList) {
                        if (spacing <= 0) {
                            pointProcessor(localId, globalId, groupId, vertices, placementPriority, 0);
                            continue;
                        }
                        
                        for (const auto& transformedPoints : generateTransformedPoints(vertices, spacing, bitmapSize, tileSize)) {
                            vt::PointLabelStyle transformedStyle(orientation, fillFunc, normalizedSizeFunc, false, bitmapImage, transformedPoints.first * (transform ? *transform : vt::Transform()));
                            pointProcessor = layerBuilder.createPointLabelProcessor(transformedStyle, glyphMap);
                            if (pointProcessor) {
                                int counter = 0;
                                for (const auto& vertex : transformedPoints.second) {
                                    long long generatedId = combineId(globalId, std::hash<vt::TileId>()(tileId) * 63 + counter);
                                    pointProcessor(localId, generatedId, groupId, vertex, placementPriority, 0);
                                    counter++;
                                }
                                pointProcessor = vt::TileLayerBuilder::PointLabelProcessor();
                            }
                        }
                    }
                }
            }
        };
    }

    std::vector<std::pair<vt::Transform, vt::TileLayerBuilder::Vertices>> MarkersSymbolizer::generateTransformedPoints(const vt::TileLayerBuilder::Vertices& vertices, float spacing, float bitmapSize, float tileSize) {
        std::vector<std::pair<vt::Transform, vt::TileLayerBuilder::Vertices>> transformedPointList;

        float linePos = 0;
        for (std::size_t i = 1; i < vertices.size(); i++) {
            const cglib::vec2<float>& v0 = vertices[i - 1];
            const cglib::vec2<float>& v1 = vertices[i];

            float lineLen = cglib::length(v1 - v0) * tileSize;
            if (spacing <= 0) {
                linePos = lineLen * 0.5f;
            }
            else if (i == 1) {
                linePos = std::min(lineLen, spacing) * 0.5f;
            }

            vt::TileLayerBuilder::Vertices points;
            while (linePos < lineLen) {
                cglib::vec2<float> pos = v0 + (v1 - v0) * (linePos / lineLen);
                if (std::min(pos(0), pos(1)) > 0.0f && std::max(pos(0), pos(1)) < 1.0f) {
                    points.push_back(pos);
                }

                if (spacing <= 0) {
                    break;
                }
                linePos += spacing + bitmapSize;
            }
            if (!points.empty()) {
                cglib::vec2<float> dir = cglib::unit(v1 - v0);
                vt::Transform transform = vt::Transform::fromMatrix2(cglib::mat2x2<float>{ { dir(0), -dir(1) }, { dir(1), dir(0) } });
                transformedPointList.emplace_back(transform, std::move(points));
            }

            linePos -= lineLen;
        }
        return transformedPointList;
    }
    
    std::shared_ptr<vt::BitmapImage> MarkersSymbolizer::makeEllipseBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor) {
        int canvasWidth = static_cast<int>(std::ceil(width + strokeWidth));
        int canvasHeight = static_cast<int>(std::ceil(height + strokeWidth));
        vt::BitmapCanvas canvas(canvasWidth, canvasHeight);
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
        vt::BitmapCanvas canvas(canvasWidth, canvasHeight);
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

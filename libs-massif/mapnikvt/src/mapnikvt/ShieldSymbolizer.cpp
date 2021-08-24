#include "ShieldSymbolizer.h"

#include <vector>
#include <tuple>

namespace carto { namespace mvt {
    ShieldSymbolizer::FeatureProcessor ShieldSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        std::shared_ptr<const vt::Font> font = getFont(symbolizerContext, exprContext);
        if (!font) {
            std::string faceName = _faceName.getValue(exprContext);
            std::string fontSetName = _fontSetName.getValue(exprContext);
            _logger->write(Logger::Severity::ERROR, "Failed to load shield font " + (!faceName.empty() ? faceName : fontSetName));
            return FeatureProcessor();
        }

        std::string file = _file.getValue(exprContext);
        std::shared_ptr<const vt::BitmapImage> backgroundImage = symbolizerContext.getBitmapManager()->loadBitmapImage(file, false, IMAGE_UPSAMPLING_SCALE);
        if (!backgroundImage || !backgroundImage->bitmap) {
            _logger->write(Logger::Severity::ERROR, "Failed to load shield bitmap " + file);
            return FeatureProcessor();
        }

        bool allowOverlap = _allowOverlap.getValue(exprContext);
        bool clip = _clip.isDefined() ? _clip.getValue(exprContext) : allowOverlap;
        float shieldDx = _shieldDx.getValue(exprContext);
        float shieldDy = _shieldDy.getValue(exprContext);

        float tileSize = symbolizerContext.getSettings().getTileSize();
        float fontScale = symbolizerContext.getSettings().getFontScale();
        float bitmapSize = static_cast<float>(std::max(backgroundImage->bitmap->width, backgroundImage->bitmap->height)) * fontScale;
        float minimumDistance = _minimumDistance.getValue(exprContext);
        float placementPriority = _placementPriority.getValue(exprContext);
        float orientationAngle = _orientationAngle.getValue(exprContext);
        float sizeStatic = _size.getStaticValue(exprContext);
        bool unlockImage = _unlockImage.getValue(exprContext);

        vt::TextFormatter textFormatter(font, sizeStatic, getFormatterOptions(symbolizerContext, exprContext));
        vt::TextFormatter::Options shieldFormatterOptions = textFormatter.getOptions();
        shieldFormatterOptions.offset = cglib::vec2<float>(shieldDx * fontScale, -shieldDy * fontScale);
        vt::TextFormatter shieldFormatter(font, sizeStatic, shieldFormatterOptions);
        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::LabelOrientation placement = getPlacement(exprContext);
        vt::LabelOrientation orientation = (placement != vt::LabelOrientation::LINE ? placement : vt::LabelOrientation::BILLBOARD_2D);
        
        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(_fill.getFunction(exprContext), _opacity.getFunction(exprContext));
        vt::FloatFunction sizeFunc = _sizeFuncBuilder.createScaledFloatFunction(_size.getFunction(exprContext), fontScale);
        vt::ColorFunction haloFillFunc = _haloFillFuncBuilder.createColorOpacityFunction(_haloFill.getFunction(exprContext), _haloOpacity.getFunction(exprContext));
        vt::FloatFunction haloRadiusFunc = _haloRadiusFuncBuilder.createScaledFloatFunction(_haloRadius.getFunction(exprContext), fontScale);

        vt::TileId tileId = exprContext.getTileId();
        std::string text = getTransformedText(exprContext);
        std::size_t hash = std::hash<std::string>()(text);

        std::optional<long long> globalIdOverride;
        if (_featureId.isDefined()) {
            globalIdOverride = convertId(_featureId.getValue(exprContext));
        }

        float textSize = bitmapSize < 0 ? (placement == vt::LabelOrientation::LINE ? calculateTextSize(textFormatter.getFont(), text, textFormatter).size()(0) : 0) : bitmapSize;
        float spacing = _spacing.getValue(exprContext);
        long long groupId = (allowOverlap ? -1 : 0);
        if (!allowOverlap && minimumDistance > 0) {
            groupId = 1;
        }

        cglib::vec2<float> backgroundOffset(0, 0);
        vt::TextFormatter formatter = (unlockImage ? textFormatter : shieldFormatter);
        if (unlockImage) {
            backgroundOffset = cglib::vec2<float>(-backgroundImage->bitmap->width * fontScale * 0.5f + shieldFormatterOptions.offset(0), -backgroundImage->bitmap->height * fontScale * 0.5f + shieldFormatterOptions.offset(1));
        }
        else {
            backgroundOffset = cglib::vec2<float>(-backgroundImage->bitmap->width * fontScale * 0.5f, -backgroundImage->bitmap->height * fontScale * 0.5f);
        }

        if (clip) {
            return [compOp, fillFunc, haloFillFunc, sizeFunc, haloRadiusFunc, fontScale, placement, text, orientationAngle, formatter, backgroundOffset, backgroundImage, spacing, textSize, tileSize, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
                vt::TextStyle style(compOp, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, orientationAngle, fontScale, backgroundOffset, backgroundImage);
                vt::TileLayerBuilder::TextProcessor textProcessor;
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (!textProcessor) {
                        textProcessor = layerBuilder.createTextProcessor(style, formatter);
                        if (!textProcessor) {
                            return;
                        }
                    }

                    if (auto pointGeometry = std::dynamic_pointer_cast<const PointGeometry>(featureCollection.getGeometry(featureIndex))) {
                        for (const auto& vertex : pointGeometry->getVertices()) {
                            textProcessor(featureCollection.getLocalId(featureIndex), vertex, text);
                        }
                    }
                    else if (placement != vt::LabelOrientation::LINE) {
                        vt::TileLayerBuilder::Vertices vertices;
                        if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(featureCollection.getGeometry(featureIndex))) {
                            vertices = lineGeometry->getMidPoints();
                        }
                        else if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(featureCollection.getGeometry(featureIndex))) {
                            vertices = polygonGeometry->getSurfacePoints();
                        }

                        for (const auto& vertex : vertices) {
                            textProcessor(featureCollection.getLocalId(featureIndex), vertex, text);
                        }
                    }
                    else {
                        vt::TileLayerBuilder::VerticesList verticesList;
                        if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(featureCollection.getGeometry(featureIndex))) {
                            verticesList = lineGeometry->getVerticesList();
                        }
                        else if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(featureCollection.getGeometry(featureIndex))) {
                            verticesList = polygonGeometry->getClosedOuterRings(true);
                        }

                        for (const auto& vertices : verticesList) {
                            for (const auto& transformedPoints : generateLinePoints(vertices, spacing, textSize, tileSize)) {
                                for (const auto& vertex : transformedPoints.second) {
                                    textProcessor(featureCollection.getLocalId(featureIndex), vertex, text);
                                }
                            }
                        }
                    }
                }
            };
        }

        return [compOp, fillFunc, haloFillFunc, sizeFunc, haloRadiusFunc, fontScale, placement, orientation, text, hash, orientationAngle, formatter, backgroundOffset, backgroundImage, spacing, textSize, tileId, tileSize, globalIdOverride, groupId, placementPriority, minimumDistance, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            vt::TextLabelStyle style(orientation, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, true, orientationAngle, fontScale, backgroundOffset, backgroundImage);
            vt::TileLayerBuilder::TextLabelProcessor textProcessor;
            for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                if (!textProcessor) {
                    textProcessor = layerBuilder.createTextLabelProcessor(style, formatter);
                    if (!textProcessor) {
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
                globalId = globalId * 3 + 2;

                if (auto pointGeometry = std::dynamic_pointer_cast<const PointGeometry>(featureCollection.getGeometry(featureIndex))) {
                    for (const auto& vertex : pointGeometry->getVertices()) {
                        textProcessor(localId, globalId, groupId, vertex, vt::TileLayerBuilder::Vertices(), text, placementPriority, minimumDistance);
                    }
                }
                else if (placement != vt::LabelOrientation::LINE) {
                    if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(featureCollection.getGeometry(featureIndex))) {
                        for (const auto& vertices : lineGeometry->getVerticesList()) {
                            textProcessor(localId, globalId, groupId, std::optional<vt::TileLayerBuilder::Vertex>(), vertices, text, placementPriority, minimumDistance);
                        }
                    }
                    else if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(featureCollection.getGeometry(featureIndex))) {
                        for (const auto& vertex : polygonGeometry->getSurfacePoints()) {
                            textProcessor(localId, globalId, groupId, vertex, vt::TileLayerBuilder::Vertices(), text, placementPriority, minimumDistance);
                        }
                    }
                }
                else {
                    vt::TileLayerBuilder::VerticesList verticesList;
                    if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(featureCollection.getGeometry(featureIndex))) {
                        verticesList = lineGeometry->getVerticesList();
                    }
                    else if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(featureCollection.getGeometry(featureIndex))) {
                        verticesList = polygonGeometry->getClosedOuterRings(true);
                    }

                    for (const auto& vertices : verticesList) {
                        if (spacing <= 0) {
                            textProcessor(localId, globalId, groupId, std::optional<vt::TileLayerBuilder::Vertex>(), vertices, text, placementPriority, minimumDistance);
                            continue;
                        }

                        for (const auto& transformedPoints : generateLinePoints(vertices, spacing, textSize, tileSize)) {
                            int counter = 0;
                            for (const auto& vertex : transformedPoints.second) {
                                long long generatedId = combineId(globalId, std::hash<vt::TileId>()(tileId) * 63 + counter);
                                textProcessor(localId, generatedId, groupId, vertex, vertices, text, placementPriority, minimumDistance);
                                counter++;
                            }
                        }
                    }
                }
            }
        };
    }
} }

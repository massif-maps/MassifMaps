#include "TextSymbolizer.h"
#include "ParserUtils.h"
#include "FontSet.h"
#include "Expression.h"
#include "StringUtils.h"
#include "vt/FontManager.h"

#include <vector>
#include <tuple>

#include <boost/math/constants/constants.hpp>

namespace carto::mvt {
    TextSymbolizer::FeatureProcessor TextSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::FloatFunction baseSizeFunc = _size.getFunction(exprContext);
        if (baseSizeFunc == vt::FloatFunction(0)) {
            return FeatureProcessor();
        }

        std::shared_ptr<const vt::Font> font = getFont(symbolizerContext, exprContext);
        if (!font) {
            std::string faceName = _faceName.getValue(exprContext);
            std::string fontSetName = _fontSetName.getValue(exprContext);
            _logger->write(Logger::Severity::ERROR, "Failed to load text font " + (!faceName.empty() ? faceName : fontSetName));
            return FeatureProcessor();
        }

        bool allowOverlap = _allowOverlap.getValue(exprContext);
        bool clip = _clip.isDefined() ? _clip.getValue(exprContext) : allowOverlap;

        float tileSize = symbolizerContext.getSettings().getTileSize();
        float fontScale = symbolizerContext.getSettings().getFontScale();
        float minimumDistance = _minimumDistance.getValue(exprContext);
        float placementPriority = _placementPriority.getValue(exprContext);
        float orientationAngle = _orientationAngle.getValue(exprContext);
        float sizeStatic = _size.getStaticValue(exprContext);

        vt::TextFormatter formatter(font, sizeStatic, getFormatterOptions(symbolizerContext, exprContext));
        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::LabelOrientation placement = getPlacement(exprContext);
        if (placement == vt::LabelOrientation::LINE) {
            orientationAngle = 0; // not supported when using line placements
        }

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

        float bitmapSize = -1;
        float textSize = bitmapSize < 0 ? (placement == vt::LabelOrientation::LINE ? calculateTextSize(formatter.getFont(), text, formatter).size()(0) : 0) : bitmapSize;
        float spacing = _spacing.getValue(exprContext);
        long long groupId = (allowOverlap ? -1 : 0);
        if (!allowOverlap && minimumDistance > 0) {
            groupId = (placement == vt::LabelOrientation::LINE ? (hash & 0x7fffffffU) : 1);
        }
        
        cglib::vec2<float> backgroundOffset(0, 0);
        std::shared_ptr<vt::BitmapImage> backgroundImage;

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

                    if (auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertex : pointGeometry->getVertices()) {
                            textProcessor(featureCollection.getLocalId(featureIndex), vertex, text);
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
                            textProcessor(featureCollection.getLocalId(featureIndex), vertex, text);
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
                            for (const auto& transformedPoints : generateLinePoints(vertices, spacing, textSize, tileSize)) {
                                vt::TextStyle transformedStyle(compOp, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, transformedPoints.first + orientationAngle, fontScale, backgroundOffset, backgroundImage);
                                textProcessor = layerBuilder.createTextProcessor(transformedStyle, formatter);
                                if (textProcessor) {
                                    for (const auto& vertex : transformedPoints.second) {
                                        textProcessor(featureCollection.getLocalId(featureIndex), vertex, text);
                                    }
                                    textProcessor = vt::TileLayerBuilder::TextProcessor();
                                }
                            }
                        }
                    }
                }
            };
        }

        return [compOp, fillFunc, haloFillFunc, sizeFunc, haloRadiusFunc, fontScale, placement, text, hash, orientationAngle, formatter, backgroundOffset, backgroundImage, spacing, textSize, tileId, tileSize, globalIdOverride, groupId, placementPriority, minimumDistance, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            vt::TextLabelStyle style(placement, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, true, orientationAngle, fontScale, backgroundOffset, backgroundImage);
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
                globalId = globalId * 3 + 1;

                if (auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                    for (const auto& vertex : pointGeometry->getVertices()) {
                        textProcessor(localId, globalId, groupId, vertex, vt::TileLayerBuilder::Vertices(), text, placementPriority, minimumDistance);
                    }
                }
                else if (placement != vt::LabelOrientation::LINE) {
                    if (auto lineGeometry = std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertices : lineGeometry->getVerticesList()) {
                            textProcessor(localId, globalId, groupId, std::optional<vt::TileLayerBuilder::Vertex>(), vertices, text, placementPriority, minimumDistance);
                        }
                    }
                    else if (auto polygonGeometry = std::get_if<PolygonGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertex : polygonGeometry->getSurfacePoints()) {
                            textProcessor(localId, globalId, groupId, vertex, vt::TileLayerBuilder::Vertices(), text, placementPriority, minimumDistance);
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

    std::vector<std::pair<float, vt::TileLayerBuilder::Vertices>> TextSymbolizer::generateLinePoints(const vt::TileLayerBuilder::Vertices& vertices, float spacing, float textSize, float tileSize) {
        std::vector<std::pair<float, vt::TileLayerBuilder::Vertices>> transformedPointList;

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
                linePos += spacing + textSize;
            }
            if (!points.empty()) {
                cglib::vec2<float> dir = cglib::unit(v1 - v0);
                float angle = std::atan2(-dir(1), dir(0));
                transformedPointList.emplace_back(angle * 180.0f / boost::math::constants::pi<float>(), std::move(points));
            }

            linePos -= lineLen;
        }
        return transformedPointList;
    }

    cglib::bbox2<float> TextSymbolizer::calculateTextSize(const std::shared_ptr<const vt::Font>& font, const std::string& text, const vt::TextFormatter& formatter) {
        std::vector<vt::Font::Glyph> glyphs = formatter.format(text, formatter.getFontSize());
        cglib::bbox2<float> bbox = cglib::bbox2<float>::smallest();
        cglib::vec2<float> pen = cglib::vec2<float>(0, 0);
        for (const vt::Font::Glyph& glyph : glyphs) {
            if (glyph.codePoint == vt::Font::CR_CODEPOINT) {
                pen = cglib::vec2<float>(0, 0);
            }
            else {
                bbox.add(pen + glyph.offset);
                bbox.add(pen + glyph.offset + glyph.size);
            }

            pen += glyph.advance;
        }
        return bbox;
    }

    vt::LabelOrientation TextSymbolizer::getPlacement(const ExpressionContext& exprContext) const {
        vt::LabelOrientation placement = _placement.getValue(exprContext);
        if (placement != vt::LabelOrientation::LINE) {
            if (_orientationAngle.isDefined()) { // if orientation is explictly defined, use POINT placement
                placement = vt::LabelOrientation::POINT;
            }
        }
        return placement;
    }

    std::string TextSymbolizer::getTransformedText(const ExpressionContext& exprContext) const {
        std::string text = _text.getValue(exprContext);
        return _textTransform.getValue(exprContext)(text);
    }

    std::shared_ptr<const vt::Font> TextSymbolizer::getFont(const SymbolizerContext& symbolizerContext, const ExpressionContext& exprContext) const {
        std::shared_ptr<const vt::Font> font = symbolizerContext.getSettings().getFallbackFont();
        std::string faceName = _faceName.getValue(exprContext);
        std::string fontSetName = _fontSetName.getValue(exprContext);
        if (!faceName.empty()) {
            font = symbolizerContext.getFontManager()->getFont(faceName, font);
        }
        else if (!fontSetName.empty()) {
            for (const std::shared_ptr<FontSet>& fontSet : _fontSets) {
                if (fontSet->getName() == fontSetName) {
                    const std::vector<std::string>& faceNames = fontSet->getFaceNames();
                    for (auto it = faceNames.rbegin(); it != faceNames.rend(); it++) {
                        const std::string& faceName = *it;
                        std::shared_ptr<const vt::Font> mainFont = symbolizerContext.getFontManager()->getFont(faceName, font);
                        if (mainFont) {
                            font = mainFont;
                        }
                    }
                    break;
                }
            }
        }
        return font;
    }

    vt::TextFormatter::Options TextSymbolizer::getFormatterOptions(const SymbolizerContext& symbolizerContext, const ExpressionContext& exprContext) const {
        float dx = _dx.getValue(exprContext);
        float dy = _dy.getValue(exprContext);
        float horizontalAlignment = _horizontalAlignment.getValue(exprContext).value_or(dx < 0 ? 1.0f : (dx > 0 ? -1.0f : 0.0f));
        float verticalAlignment = _verticalAlignment.getValue(exprContext).value_or(dy < 0 ? 1.0f : (dy > 0 ? -1.0f : 0.0f));
        float fontScale = symbolizerContext.getSettings().getFontScale();
        float characterSpacing = _characterSpacing.getValue(exprContext);
        float lineSpacing = _lineSpacing.getValue(exprContext);
        float wrapWidth = _wrapWidth.getValue(exprContext);
        bool wrapBefore = _wrapBefore.getValue(exprContext);
        std::string wrapCharacter = _wrapCharacter.getValue(exprContext);

        cglib::vec2<float> offset(dx * fontScale, -dy * fontScale);
        cglib::vec2<float> alignment(horizontalAlignment, verticalAlignment);
        return vt::TextFormatter::Options(alignment, offset, wrapCharacter, wrapBefore, wrapWidth * fontScale, characterSpacing, lineSpacing);
    }
}

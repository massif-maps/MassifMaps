#include "TextSymbolizer.h"
#include "ParserUtils.h"
#include "FontSet.h"
#include "Expression.h"
#include "StringUtils.h"
#include "vt/FontManager.h"

#include <vector>
#include <tuple>

namespace carto { namespace mvt {
    void TextSymbolizer::build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const {
        vt::FloatFunction baseSizeFunc = _size.getFunction(exprContext);
        if (baseSizeFunc == vt::FloatFunction(0)) {
            return;
        }
        
        std::shared_ptr<const vt::Font> font = getFont(symbolizerContext, exprContext);
        if (!font) {
            std::string faceName = _faceName.getValue(exprContext);
            std::string fontSetName = _fontSetName.getValue(exprContext);
            _logger->write(Logger::Severity::ERROR, "Failed to load text font " + (!faceName.empty() ? faceName : fontSetName));
            return;
        }

        bool allowOverlap = _allowOverlap.getValue(exprContext);
        bool clip = _clip.isDefined() ? _clip.getValue(exprContext) : allowOverlap;

        float fontScale = symbolizerContext.getSettings().getFontScale();
        float minimumDistance = _minimumDistance.getValue(exprContext) * std::pow(2.0f, -exprContext.getAdjustedZoom());
        float placementPriority = _placementPriority.getValue(exprContext);
        float orientationAngle = _orientationAngle.getValue(exprContext);
        float sizeStatic = _size.getStaticValue(exprContext);

        vt::TextFormatter formatter(font, sizeStatic, getFormatterOptions(symbolizerContext, exprContext));
        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::LabelOrientation placement = getPlacement(exprContext);

        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(_fill.getFunction(exprContext), _opacity.getFunction(exprContext));
        vt::FloatFunction sizeFunc = _sizeFuncBuilder.createScaledFloatFunction(_size.getFunction(exprContext), fontScale);
        vt::ColorFunction haloFillFunc = _haloFillFuncBuilder.createColorOpacityFunction(_haloFill.getFunction(exprContext), _haloOpacity.getFunction(exprContext));
        vt::FloatFunction haloRadiusFunc = _haloRadiusFuncBuilder.createScaledFloatFunction(_haloRadius.getFunction(exprContext), fontScale);

        std::vector<std::pair<long long, std::tuple<vt::TileLayerBuilder::Vertex, std::string>>> textInfos;
        std::vector<std::pair<long long, vt::TileLayerBuilder::TextLabelInfo>> labelInfos;

        auto addText = [&](long long localId, long long globalId, const std::string& text, const std::optional<vt::TileLayerBuilder::Vertex>& vertex, const vt::TileLayerBuilder::Vertices& vertices) {
            long long groupId = (allowOverlap ? -1 : (minimumDistance > 0 ? (std::hash<std::string>()(text) & 0x7fffffff) : 0));
            
            if (clip) {
                if (vertex) {
                    textInfos.emplace_back(localId, std::make_tuple(*vertex, text));
                }
                else if (!vertices.empty()) {
                    textInfos.emplace_back(localId, std::make_tuple(vertices.front(), text));
                }
            }
            else {
                labelInfos.emplace_back(localId, vt::TileLayerBuilder::TextLabelInfo(globalId * 3 + 1, groupId, text, vertex, vertices, placementPriority, minimumDistance));
            }
        };

        auto flushTexts = [&]() {
            if (clip) {
                vt::TextStyle style(compOp, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, orientationAngle, fontScale, cglib::vec2<float>(0, 0), std::shared_ptr<vt::BitmapImage>());

                std::size_t textInfoIndex = 0;
                layerBuilder.addTexts([&](long long& id, vt::TileLayerBuilder::Vertex& vertex, std::string& text) {
                    if (textInfoIndex >= textInfos.size()) {
                        return false;
                    }
                    id = textInfos[textInfoIndex].first;
                    vertex = std::get<0>(textInfos[textInfoIndex].second);
                    text = std::get<1>(textInfos[textInfoIndex].second);
                    textInfoIndex++;
                    return true;
                }, style, formatter);

                textInfos.clear();
            }
            else {
                vt::TextLabelStyle style(placement, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, true, orientationAngle, fontScale, cglib::vec2<float>(0, 0), std::shared_ptr<vt::BitmapImage>());

                std::size_t labelInfoIndex = 0;
                layerBuilder.addTextLabels([&](long long& id, vt::TileLayerBuilder::TextLabelInfo& labelInfo) {
                    if (labelInfoIndex >= labelInfos.size()) {
                        return false;
                    }
                    id = labelInfos[labelInfoIndex].first;
                    labelInfo = labelInfos[labelInfoIndex].second;
                    labelInfoIndex++;
                    return true;
                }, style, formatter);

                labelInfos.clear();
            }
        };

        buildFeatureCollection(featureCollection, exprContext, symbolizerContext, formatter, placement, -1, addText);

        flushTexts();
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
        std::string textTransform = toLower(_textTransform.getValue(exprContext));
        if (textTransform.empty()) {
            return text;
        }
        else if (textTransform == "uppercase") {
            return toUpper(text);
        }
        else if (textTransform == "lowercase") {
            return toLower(text);
        }
        else if (textTransform == "capitalize") {
            return capitalize(text);
        }
        return text;
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
        float fontScale = symbolizerContext.getSettings().getFontScale();
        float dx = _dx.getValue(exprContext);
        float dy = _dy.getValue(exprContext);
        float characterSpacing = _characterSpacing.getValue(exprContext);
        float lineSpacing = _lineSpacing.getValue(exprContext);
        float wrapWidth = _wrapWidth.getValue(exprContext);
        bool wrapBefore = _wrapBefore.getValue(exprContext);
        std::string horizontalAlignment = _horizontalAlignment.getValue(exprContext);
        std::string verticalAlignment = _verticalAlignment.getValue(exprContext);

        cglib::vec2<float> offset(dx * fontScale, -dy * fontScale);
        cglib::vec2<float> alignment(dx < 0 ? 1.0f : (dx > 0 ? -1.0f : 0.0f), dy < 0 ? 1.0f : (dy > 0 ? -1.0f : 0.0f));
        if (horizontalAlignment == "left") {
            alignment(0) = -1.0f;
        }
        else if (horizontalAlignment == "middle") {
            alignment(0) = 0.0f;
        }
        else if (horizontalAlignment == "right") {
            alignment(0) = 1.0f;
        }
        if (verticalAlignment == "top") {
            alignment(1) = -1.0f;
        }
        else if (verticalAlignment == "middle") {
            alignment(1) = 0.0f;
        }
        else if (verticalAlignment == "bottom") {
            alignment(1) = 1.0f;
        }
        return vt::TextFormatter::Options(alignment, offset, wrapBefore, wrapWidth * fontScale, characterSpacing, lineSpacing);
    }

    void TextSymbolizer::buildFeatureCollection(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, const vt::TextFormatter& formatter, vt::LabelOrientation placement, float bitmapSize, const std::function<void(long long localId, long long globalId, const std::string& text, const std::optional<vt::TileLayerBuilder::Vertex>& vertex, const vt::TileLayerBuilder::Vertices& vertices)>& addText) const {
        for (std::size_t index = 0; index < featureCollection.size(); index++) {
            std::string text = getTransformedText(exprContext);
            std::size_t hash = std::hash<std::string>()(text);

            long long localId = featureCollection.getLocalId(index);
            long long globalId = combineId(featureCollection.getGlobalId(index), hash);
            if (_featureId.isDefined()) {
                globalId = convertId(_featureId.getValue(exprContext));
                if (!globalId) {
                    globalId = generateId();
                }
            }

            const std::shared_ptr<const Geometry>& geometry = featureCollection.getGeometry(index);
            float textSize = bitmapSize < 0 ? (placement == vt::LabelOrientation::LINE ? calculateTextSize(formatter.getFont(), text, formatter).size()(0) : 0) : bitmapSize;
            float spacing = _spacing.getValue(exprContext);

            auto addLineTexts = [&](const std::vector<cglib::vec2<float>>& vertices) {
                if (spacing <= 0) {
                    addText(localId, globalId, text, std::optional<vt::TileLayerBuilder::Vertex>(), vertices);
                    return;
                }

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
                            addText(localId, generateId(), text, pos, vertices);
                        }

                        linePos += spacing + textSize;
                    }

                    linePos -= lineLen;
                }
            };

            if (auto pointGeometry = std::dynamic_pointer_cast<const PointGeometry>(geometry)) {
                for (const auto& vertex : pointGeometry->getVertices()) {
                    addText(localId, globalId, text, vertex, vt::TileLayerBuilder::Vertices());
                }
            }
            else if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(geometry)) {
                if (placement == vt::LabelOrientation::LINE) {
                    for (const auto& vertices : lineGeometry->getVerticesList()) {
                        addLineTexts(vertices);
                    }
                }
                else {
                    for (const auto& vertices : lineGeometry->getVerticesList()) {
                        addText(localId, globalId, text, std::optional<vt::TileLayerBuilder::Vertex>(), vertices);
                    }
                }
            }
            else if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(geometry)) {
                if (placement == vt::LabelOrientation::LINE) {
                    for (const auto& vertices : polygonGeometry->getClosedOuterRings(true)) {
                        addLineTexts(vertices);
                    }
                }
                else {
                    for (const auto& vertex : polygonGeometry->getSurfacePoints()) {
                        addText(localId, globalId, text, vertex, vt::TileLayerBuilder::Vertices());
                    }
                }
            }
            else {
                _logger->write(Logger::Severity::WARNING, "Unsupported geometry for TextSymbolizer/ShieldSymbolizer");
            }
        }
    }
} }

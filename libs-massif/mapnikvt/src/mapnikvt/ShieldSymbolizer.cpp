#include "ShieldSymbolizer.h"

#include <vector>
#include <tuple>
#include <cctype>
#include <cmath>

#include <utf8.h>

namespace massif::mvt {
    std::vector<vt::LabelAnchor> ShieldSymbolizer::parseAnchors(const std::string& anchors) {
        static const std::pair<const char*, vt::LabelAnchor> anchorTable[] = {
            { "center",       vt::LabelAnchor::CENTER },
            { "top",          vt::LabelAnchor::TOP },
            { "bottom",       vt::LabelAnchor::BOTTOM },
            { "left",         vt::LabelAnchor::LEFT },
            { "right",        vt::LabelAnchor::RIGHT },
            { "topleft",      vt::LabelAnchor::TOP_LEFT },
            { "topright",     vt::LabelAnchor::TOP_RIGHT },
            { "bottomleft",   vt::LabelAnchor::BOTTOM_LEFT },
            { "bottomright",  vt::LabelAnchor::BOTTOM_RIGHT }
        };

        std::vector<vt::LabelAnchor> result;
        std::string name;
        auto flush = [&name, &result]() {
            if (name.empty()) {
                return;
            }
            for (const auto& entry : anchorTable) {
                if (name == entry.first) {
                    if (std::find(result.begin(), result.end(), entry.second) == result.end()) {
                        result.push_back(entry.second);
                    }
                    break;
                }
            }
            name.clear();
        };
        for (char c : anchors) {
            if (c == ',' || c == ' ' || c == '\t') {
                flush();
            }
            else if (c != '-' && c != '_') { // 'top-left', 'top_left' and 'topleft' are the same side
                name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        flush();
        return result;
    }

    vt::LabelLineAlign ShieldSymbolizer::parseLineAlign(const std::string& align) {
        std::string name;
        for (char c : align) {
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (name == "left") {
            return vt::LabelLineAlign::LEFT;
        }
        if (name == "right") {
            return vt::LabelLineAlign::RIGHT;
        }
        if (name == "auto") {
            return vt::LabelLineAlign::AUTO;
        }
        return vt::LabelLineAlign::CENTER; // 'middle' and anything unset
    }

    std::vector<vt::Font::Glyph> ShieldSymbolizer::buildIconGlyphs(const std::shared_ptr<const vt::Font>& font, const SymbolizerContext& symbolizerContext, const ExpressionContext& exprContext, float fontSize) const {
        std::vector<vt::Font::Glyph> glyphs;
        std::string iconText = _iconText.getValue(exprContext);
        std::string iconFaceName = _iconFaceName.getValue(exprContext);
        if (iconText.empty() || iconFaceName.empty() || !font) {
            return glyphs;
        }

        const SymbolizerContext::Settings& settings = symbolizerContext.getSettings();
        float fontScale = settings.getFontScale();
        float iconSize = _iconSize.getValue(exprContext) * fontScale;

        // The icon face has to be reached THROUGH the label font, or its glyphs land in an atlas of
        // its own and the label - which is drawn from a single atlas - cannot show them.
        std::shared_ptr<const vt::Font> iconFace = symbolizerContext.getFontManager()->getFont(iconFaceName, std::shared_ptr<const vt::Font>());
        if (!iconFace) {
            _logger->write(Logger::Severity::ERROR, "Failed to load shield icon font " + iconFaceName);
            return glyphs;
        }
        // Rasterized for the size the ICON is drawn at, not the text's: a fallback rasterizes its
        // own glyphs at its own render size and shapeGlyphs scales the metrics for it, so a large
        // icon next to small text is not a magnified 19px raster.
        float iconSizePixels = (iconSize > 0 ? iconSize : fontSize * fontScale) * settings.getPixelScale();
        if (std::shared_ptr<const vt::Font> sizedFace = symbolizerContext.getFontManager()->getFont(iconFace, vt::pickGlyphRenderSize(iconSizePixels))) {
            iconFace = sizedFace;
        }
        std::shared_ptr<const vt::Font> iconFont = symbolizerContext.getFontManager()->getFont(font->getName(), iconFace);
        if (!iconFont) {
            return glyphs;
        }

        std::vector<std::uint32_t> utf32Text;
        utf32Text.reserve(iconText.size());
        utf8::utf8to32(iconText.begin(), iconText.end(), std::back_inserter(utf32Text));
        if (utf32Text.empty()) {
            return glyphs;
        }
        glyphs = iconFont->shapeGlyphs(utf32Text.data(), utf32Text.size(), 1.0f, false);
        if (glyphs.empty()) {
            return glyphs;
        }

        float scale = (iconSize > 0 && fontSize > 0 ? iconSize / fontSize : 1.0f);
        cglib::bbox2<float> bbox = cglib::bbox2<float>::smallest();
        cglib::vec2<float> pen(0, 0);
        for (vt::Font::Glyph& glyph : glyphs) {
            glyph.offset *= scale;
            glyph.size *= scale;
            glyph.advance *= scale;
            glyph.icon = true;
            bbox.add(pen + glyph.offset);
            bbox.add(pen + glyph.offset + glyph.size);
            pen += glyph.advance;
        }

        // Centred on the anchor, then moved by the style's own offset - the shield bitmap is placed
        // the same way (backgroundOffset), so an icon and a plate sit on top of each other.
        float invFontSize = (fontSize > 0 ? 1.0f / fontSize : 0.0f);
        cglib::vec2<float> shift(-(bbox.min(0) + bbox.max(0)) * 0.5f + _iconDx.getValue(exprContext) * fontScale * invFontSize,
                                 -(bbox.min(1) + bbox.max(1)) * 0.5f - _iconDy.getValue(exprContext) * fontScale * invFontSize);
        for (vt::Font::Glyph& glyph : glyphs) {
            glyph.offset += shift;
        }
        return glyphs;
    }


    ShieldSymbolizer::FeatureProcessor ShieldSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        std::shared_ptr<const vt::Font> font = getFont(symbolizerContext, exprContext);
        if (!font) {
            std::string faceName = _faceName.getValue(exprContext);
            std::string fontSetName = _fontSetName.getValue(exprContext);
            _logger->write(Logger::Severity::ERROR, "Failed to load shield font " + (!faceName.empty() ? faceName : fontSetName));
            return FeatureProcessor();
        }

        std::string file = _file.getValue(exprContext);
        std::shared_ptr<const vt::BitmapImage> backgroundImage;
        if (!file.empty()) {
            backgroundImage = symbolizerContext.getBitmapManager()->loadBitmapImage(file, IMAGE_UPSAMPLING_SCALE);
        }
        // if (!backgroundImage || !backgroundImage->bitmap) {
        //     _logger->write(Logger::Severity::ERROR, "Failed to load shield bitmap " + file);
        //     return FeatureProcessor();
        // }

        bool allowOverlap = _allowOverlap.getValue(exprContext);
        // Its own property, not a synonym for allow-overlap - see TextSymbolizer. A shield IS a
        // label (icon plus name, placed together), so clipping it costs the whole placement.
        bool clip = _clip.getValue(exprContext);
        float shieldDx = _shieldDx.getValue(exprContext);
        float shieldDy = _shieldDy.getValue(exprContext);

        float tileSize = symbolizerContext.getSettings().getTileSize();
        float fontScale = symbolizerContext.getSettings().getFontScale();
        float bitmapSize = 0;
        if (backgroundImage && backgroundImage->bitmap) {
            bitmapSize = static_cast<float>(std::max(backgroundImage->bitmap->width, backgroundImage->bitmap->height)) * fontScale;
        }
        float minimumDistance = _minimumDistance.getValue(exprContext);
        float maxDistance = _maxDistance.getValue(exprContext);
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
        
        std::vector<vt::LabelAnchor> anchors = parseAnchors(_anchors.getValue(exprContext));
        vt::LabelLineAlign textLineAlign = parseLineAlign(_textHorizontalAlignment.getValue(exprContext));
        vt::LabelPlateStyle textPlate = getPlateStyle(symbolizerContext, exprContext);
        vt::LabelPlateStyle iconPlate;
        iconPlate.color = vt::Color::fromColorOpacity(_iconBackgroundFill.getValue(exprContext), _iconBackgroundOpacity.getValue(exprContext));
        iconPlate.radius = _iconBackgroundRadius.getValue(exprContext) * fontScale;
        iconPlate.padding = cglib::vec2<float>(_iconBackgroundPaddingX.getValue(exprContext) * fontScale, _iconBackgroundPaddingY.getValue(exprContext) * fontScale);
        iconPlate.borderColor = vt::Color::fromColorOpacity(_iconBackgroundBorderFill.getValue(exprContext), _iconBackgroundBorderOpacity.getValue(exprContext));
        iconPlate.borderWidth = _iconBackgroundBorderWidth.getValue(exprContext) * fontScale;
        bool textOptional = _textOptional.getValue(exprContext);
        std::vector<vt::Font::Glyph> iconGlyphs = buildIconGlyphs(font, symbolizerContext, exprContext, sizeStatic);
        std::optional<vt::ColorFunction> iconColorFunc;
        if (_iconFill.isDefined() && !iconGlyphs.empty()) {
            iconColorFunc = _iconFillFuncBuilder.createColorOpacityFunction(_iconFill.getFunction(exprContext), _iconOpacity.getFunction(exprContext));
        }

        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(_fill.getFunction(exprContext), _opacity.getFunction(exprContext));
        vt::FloatFunction sizeFunc = _sizeFuncBuilder.createScaledFloatFunction(_size.getFunction(exprContext), fontScale);
        vt::ColorFunction haloFillFunc = _haloFillFuncBuilder.createColorOpacityFunction(_haloFill.getFunction(exprContext), _haloOpacity.getFunction(exprContext));
        vt::FloatFunction haloRadiusFunc = _haloRadiusFuncBuilder.createScaledFloatFunction(_haloRadius.getFunction(exprContext), fontScale);

        vt::TileId tileId = exprContext.getTileId();
        std::string text = getTransformedText(exprContext);
        std::size_t hash = std::hash<std::string>()(text);

        std::optional<long long> labelIdOverride;
        if (_featureId.isDefined()) {
            labelIdOverride = convertId(_featureId.getValue(exprContext));
        }

        float textSize = bitmapSize < 0 ? (placement == vt::LabelOrientation::LINE ? calculateTextSize(textFormatter.getFont(), text, textFormatter).size()(0) : 0) : bitmapSize;
        float spacing = _spacing.getValue(exprContext);
        long long groupId = (allowOverlap ? -1 : 0);
        if (!allowOverlap && minimumDistance > 0) {
            groupId = 1;
        }

        cglib::vec2<float> backgroundOffset(0, 0);
        vt::TextFormatter formatter = (unlockImage ? textFormatter : shieldFormatter);
        if (backgroundImage && backgroundImage->bitmap) {
            if (unlockImage) {
                backgroundOffset = cglib::vec2<float>(-backgroundImage->bitmap->width * fontScale * 0.5f + shieldFormatterOptions.offset(0), -backgroundImage->bitmap->height * fontScale * 0.5f + shieldFormatterOptions.offset(1));
            }
            else {
                backgroundOffset = cglib::vec2<float>(-backgroundImage->bitmap->width * fontScale * 0.5f, -backgroundImage->bitmap->height * fontScale * 0.5f);
            }
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

                    if (auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        auto verticesList = pointGeometry->getVerticesList();
                        int index = 0;
                        for (const auto& vertices : verticesList) {
                            for (const auto &vertex: vertices) {
                                textProcessor(featureCollection.getLocalId(featureIndex), vertex, text, index);
                            }
                            index++;
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
                            textProcessor(featureCollection.getLocalId(featureIndex), vertex, text, 0);
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
                            for (const auto& transformedPoints : generateLinePoints(vertices, spacing, textSize, tileSize, false)) {
                                for (const auto& vertex : transformedPoints.second) {
                                    textProcessor(featureCollection.getLocalId(featureIndex), vertex, text, 0);
                                }
                            }
                        }
                    }
                }
            };
        }

        return [compOp, fillFunc, haloFillFunc, sizeFunc, haloRadiusFunc, fontScale, placement, orientation, text, hash, orientationAngle, formatter, backgroundOffset, backgroundImage, spacing, textSize, tileId, tileSize, labelIdOverride, groupId, placementPriority, minimumDistance, maxDistance, anchors, textOptional, iconGlyphs, iconColorFunc, textLineAlign, textPlate, iconPlate, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            vt::TextLabelStyle style(orientation, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, true, orientationAngle, fontScale, backgroundOffset, backgroundImage, maxDistance);
            style.anchors = anchors;
            style.textOptional = textOptional;
            style.iconGlyphs = iconGlyphs;
            style.iconColorFunc = iconColorFunc;
            style.textLineAlign = textLineAlign;
            style.textPlate = textPlate;
            style.iconPlate = iconPlate;
            vt::TileLayerBuilder::TextLabelProcessor textProcessor;
            for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                if (!textProcessor) {
                    textProcessor = layerBuilder.createTextLabelProcessor(style, formatter);
                    if (!textProcessor) {
                        return;
                    }
                }

                long long localId = featureCollection.getLocalId(featureIndex);
                long long labelId = combineId(featureCollection.getFeatureId(featureIndex), hash);
                if (labelIdOverride) {
                    labelId = *labelIdOverride;
                    if (!labelId) {
                        labelId = generateId();
                    }
                }

                if (auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                    auto verticesList = pointGeometry->getVerticesList();
                    int index = 0;
                    for (const auto& vertices : verticesList) {
                        for (const auto &vertex: vertices) {
                            textProcessor(localId, 10 * labelId + index, groupId, vertex, vt::TileLayerBuilder::Vertices(), text, placementPriority, minimumDistance, false, false, index);
                        }
                        index++;
                    }
                }
                else if (placement != vt::LabelOrientation::LINE) {
                    if (auto lineGeometry = std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertices : lineGeometry->getVerticesList()) {
                            textProcessor(localId, labelId, groupId, std::optional<vt::TileLayerBuilder::Vertex>(), vertices, text, placementPriority, minimumDistance, false, false, 0);
                        }
                    }
                    else if (auto polygonGeometry = std::get_if<PolygonGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertex : polygonGeometry->getSurfacePoints()) {
                            textProcessor(localId, labelId, groupId, vertex, vt::TileLayerBuilder::Vertices(), text, placementPriority, minimumDistance, false, false, 0);
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

                    // One counter for the WHOLE feature: it makes the id of each repeat along the line
                    // unique. Restarting it per segment (generateLinePoints returns one entry per
                    // segment) gave the same id to one repeat in every segment, and labels sharing an
                    // id are merged into a single one - so text-spacing placed the repeats and then
                    // collapsed them, leaving one label per line.
                    int counter = 0;
                    for (const auto& vertices : verticesList) {
                        if (spacing <= 0) {
                            textProcessor(localId, labelId, groupId, std::optional<vt::TileLayerBuilder::Vertex>(), vertices, text, placementPriority, minimumDistance, false, false, 0);
                            continue;
                        }

                        for (const auto& transformedPoints : generateLinePoints(vertices, spacing, textSize, tileSize, false)) {
                            for (const auto& vertex : transformedPoints.second) {
                                long long generatedLabelId = combineId(labelId, std::hash<vt::TileId>()(tileId) * 63 + counter);
                                textProcessor(localId, generatedLabelId, groupId, vertex, vertices, text, placementPriority, minimumDistance, false, false, 0);
                                counter++;
                            }
                        }
                    }
                }
            }
        };
    }
}

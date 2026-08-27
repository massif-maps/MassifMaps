#include "TextSymbolizer.h"

#include "ParseTables.h"
#include "ParserUtils.h"
#include "FontSet.h"
#include "Expression.h"
#include "StringUtils.h"
#include "vt/FontManager.h"
#include "vt/FontNames.h"

#include <vector>
#include <tuple>

#include <boost/math/constants/constants.hpp>

namespace {
    // Unset (and unknown) leaves the label laid out around its own anchor, which is what a style
    // that says nothing about callout anchoring gets.
    std::optional<cglib::vec2<float>> parseBoxAnchor(const std::string& name) {
        const massif::mvt::ParseTable<cglib::vec2<float>>& table = massif::mvt::getLabelBoxAnchorTable();
        auto it = table.find(name);
        return it != table.end() ? std::optional<cglib::vec2<float>>(it->second) : std::optional<cglib::vec2<float>>();
    }
}

namespace massif::mvt {
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
        bool allowOverlapSameFeatureId = _allowOverlapSameFeatureId.getValue(exprContext);
        bool sameFeatureIdDependent = _sameFeatureIdDependent.getValue(exprContext);
        // 'clip' is its own property, and it means something entirely different from allow-overlap:
        // clipped text leaves the label pipeline for the tile geometry, so it gets no culler, no
        // text-min-distance, no run following the line and no placement - it is cut at the tile
        // border instead. Upstream defaulted it to allow-overlap (2019, no rationale recorded),
        // which handed the geometry path to every style that only wanted its labels to overlap.
        bool clip = _clip.getValue(exprContext);

        float tileSize = symbolizerContext.getSettings().getTileSize();
        float fontScale = symbolizerContext.getSettings().getFontScale();
        float pixelScale = symbolizerContext.getSettings().getPixelScale();
        // The culler measures in DEVICE pixels, so this takes the pixel scale the way emSizePixels
        // and iconSizePixels do. dx/dy, halo-radius and wrap-width beside it take fontScale alone
        // because they are in GLYPH units - the formatter divides them by the font size. Left
        // unscaled, a style's separation shrank to a third of what it asked for on a hi-dpi screen.
        float minimumDistance = _minimumDistance.getValue(exprContext) * fontScale * pixelScale;
        float maxDistance = _maxDistance.getValue(exprContext);
        float occlusionOpacity = _occlusionOpacity.getValue(exprContext);
        float placementPriority = _placementPriority.getValue(exprContext);
        float calloutScreenAnchor = _calloutScreenAnchor.getValue(exprContext);
        float calloutOffset = _calloutOffset.getValue(exprContext) * fontScale;
        float calloutStep = _calloutStep.getValue(exprContext) * fontScale;
        int calloutMaxRows = static_cast<int>(_calloutMaxRows.getValue(exprContext));
        int calloutPersistPasses = static_cast<int>(_calloutPersist.getValue(exprContext));
        float calloutLineWidth = _calloutLineWidth.getValue(exprContext) * fontScale;
        std::optional<cglib::vec2<float>> calloutLineAnchor = parseBoxAnchor(_calloutLineAnchor.getValue(exprContext));
        std::optional<cglib::vec2<float>> calloutBandAnchor = parseBoxAnchor(_calloutAlign.getValue(exprContext));
        vt::FloatFunction rankFunc = _rank.getFunction(exprContext);
        std::optional<vt::ColorFunction> secondaryColorFunc;
        if (_secondaryFill.isDefined() || _secondaryOpacity.isDefined()) {
            secondaryColorFunc = _secondaryFillFuncBuilder.createColorOpacityFunction(_secondaryFill.getFunction(exprContext), _secondaryOpacity.getFunction(exprContext));
        }
        vt::LabelPlateStyle textPlate = getPlateStyle(symbolizerContext, exprContext);
        float orientationAngle = _orientationAngle.getValue(exprContext);
        float sizeStatic = _size.getStaticValue(exprContext);

        vt::TextFormatter formatter(font, sizeStatic, getFormatterOptions(symbolizerContext, exprContext));
        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::LabelOrientation placement = getPlacement(exprContext);
        if (placement == vt::LabelOrientation::LINE || placement == vt::LabelOrientation::LINE_BILLBOARD_3D) {
            orientationAngle = 0; // not supported when using line placements
        }

        vt::ColorFunction fillFunc = _fillFuncBuilder.createColorOpacityFunction(_fill.getFunction(exprContext), _opacity.getFunction(exprContext));
        vt::FloatFunction sizeFunc = _sizeFuncBuilder.createScaledFloatFunction(_size.getFunction(exprContext), fontScale);
        vt::ColorFunction haloFillFunc = _haloFillFuncBuilder.createColorOpacityFunction(_haloFill.getFunction(exprContext), _haloOpacity.getFunction(exprContext));
        // Style pixels, like the text size beside it: the halo has to keep its width RELATIVE to the
        // glyphs on every display, and the renderer measures it in device pixels. Left unscaled it
        // shrank against its own text as the dpi rose (1.2 drew 1.8 px where mapbox draws 3.2 on a
        // 2.6x screen).
        vt::FloatFunction haloRadiusFunc = _haloRadiusFuncBuilder.createScaledFloatFunction(_haloRadius.getFunction(exprContext), fontScale * pixelScale);

        vt::TileId tileId = exprContext.getTileId();
        std::string text = getTransformedText(exprContext);
        std::size_t hash = std::hash<std::string>()(text);

        std::optional<long long> labelIdOverride;
        if (_featureId.isDefined()) {
            labelIdOverride = convertId(_featureId.getValue(exprContext));
        }

        float bitmapSize = -1;
        // Every line placement walks the line at 'spacing' and is measured with the run length a
        // line placement needs. Only the two that lay a glyph RUN out get the line itself
        // (vt::Label::isLineRun); 'billboard-line-repeat' puts one upright billboard per step.
        bool billboardRepeat = (placement == vt::LabelOrientation::LINE_BILLBOARD_REPEAT);
        bool repeatAlongLine = (placement == vt::LabelOrientation::LINE || placement == vt::LabelOrientation::LINE_BILLBOARD_3D || billboardRepeat);
        bool lineRun = (repeatAlongLine && !billboardRepeat);
        // What vt is given: the repeat is a billboard, and vt has no case for anything else.
        vt::LabelOrientation orientation = (billboardRepeat ? vt::LabelOrientation::BILLBOARD_3D : placement);
        float textSize = bitmapSize < 0 ? (repeatAlongLine ? calculateTextSize(formatter.getFont(), text, formatter).size()(0) : 0) : bitmapSize;
        float spacing = _spacing.getValue(exprContext);
        // A repeat must not stack on itself. 'spacing' is walked per TILE, over that tile's clipped
        // copy of the line, so each tile starts its own phase and two anchors can land a few pixels
        // apart across a tile border - one road shield drawn twice. Nothing in the decode can see
        // that; only the culler can, and it needs a group distance to do it. Without an explicit
        // minimum the label's own size is the floor, in the screen pixels the culler measures.
        if (repeatAlongLine && spacing > 0 && !_minimumDistance.isDefined()) {
            minimumDistance = sizeStatic * fontScale;
        }
        long long groupId = (allowOverlap ? -1 : 0);
        if (!allowOverlap && minimumDistance > 0) {
            groupId = (repeatAlongLine ? (hash & 0x7fffffffU) : 1);
        }
        
        cglib::vec2<float> backgroundOffset(0, 0);
        std::shared_ptr<vt::BitmapImage> backgroundImage;

        if (clip) {
            return [compOp, fillFunc, haloFillFunc, sizeFunc, haloRadiusFunc, fontScale, repeatAlongLine, billboardRepeat, lineRun, text, orientationAngle, formatter, backgroundOffset, backgroundImage, spacing, textSize, tileSize, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
                vt::TextStyle style(compOp, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, orientationAngle, fontScale, backgroundOffset, backgroundImage);
                vt::TileLayerBuilder::TextProcessor textProcessor;
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (!textProcessor) {
                        textProcessor = layerBuilder.createTextProcessor(style, formatter);
                        if (!textProcessor) {
                            return;
                        }
                    }

                    // 'billboard-line-repeat' walks a LINE only: on any other geometry it is the
                    // plain billboard its name says.
                    bool repeat = repeatAlongLine && (!billboardRepeat || std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get()) != nullptr);

                    if (auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        long long localId = featureCollection.getLocalId(featureIndex);
                        auto verticesList = pointGeometry->getVerticesList();

                        int index = 0;
                        for (const auto& vertices : verticesList) {
                            for (const auto &vertex: vertices) {
                                textProcessor(localId, vertex, text, index);
                            }
                            index++;
                        }
                    }
                    else if (!repeat) {
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
                            // A repeat is not rotated by its segment: clipped text has no camera to
                            // face, so all a billboard can keep here is the style's own angle.
                            for (const auto& transformedPoints : generateLinePoints(vertices, spacing, textSize, tileSize, lineRun)) {
                                vt::TextStyle transformedStyle(compOp, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, transformedPoints.first + orientationAngle, fontScale, backgroundOffset, backgroundImage);
                                textProcessor = layerBuilder.createTextProcessor(transformedStyle, formatter);
                                if (textProcessor) {
                                    for (const auto& vertex : transformedPoints.second) {
                                        textProcessor(featureCollection.getLocalId(featureIndex), vertex, text, 0);
                                    }
                                    textProcessor = vt::TileLayerBuilder::TextProcessor();
                                }
                            }
                        }
                    }
                }
            };
        }

        return [compOp, fillFunc, haloFillFunc, sizeFunc, haloRadiusFunc, fontScale, orientation, repeatAlongLine, billboardRepeat, lineRun, text, hash, orientationAngle, formatter, backgroundOffset, backgroundImage, spacing, textSize, tileId, tileSize, labelIdOverride, groupId, placementPriority, minimumDistance, maxDistance, occlusionOpacity, secondaryColorFunc, rankFunc, calloutScreenAnchor, calloutOffset, calloutStep, calloutMaxRows, calloutPersistPasses, calloutLineWidth, calloutLineAnchor, calloutBandAnchor, textPlate, allowOverlapSameFeatureId, sameFeatureIdDependent, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            vt::TextLabelStyle style(orientation, fillFunc, sizeFunc, haloFillFunc, haloRadiusFunc, true, orientationAngle, fontScale, backgroundOffset, backgroundImage, maxDistance, secondaryColorFunc, rankFunc, calloutScreenAnchor, calloutOffset, calloutStep, calloutMaxRows, calloutPersistPasses, calloutLineWidth, calloutLineAnchor, calloutBandAnchor, textPlate);
            if (occlusionOpacity >= 0.0f) {
                style.occlusionOpacity = occlusionOpacity;
            }
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
                bool labelIdOverriden = false;
                if (labelIdOverride) {
                    labelId = *labelIdOverride;
                    if (!labelId) {
                        labelId = generateId();
                    } else {
                        labelIdOverriden = true;
                    }
                }

                // 'billboard-line-repeat' walks a LINE only: on any other geometry it is the plain
                // billboard its name says.
                bool repeat = repeatAlongLine && (!billboardRepeat || std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get()) != nullptr);

                if (auto pointGeometry = std::get_if<PointGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                    auto verticesList = pointGeometry->getVerticesList();
                    int index = 0;
                    for (const auto& vertices : verticesList) {
                        for (const auto &vertex: vertices) {
                            textProcessor(localId, 10 * labelId + index, groupId, vertex,
                                          vt::TileLayerBuilder::Vertices(), text, placementPriority,
                                          minimumDistance, allowOverlapSameFeatureId,
                                          sameFeatureIdDependent, index);
                        }
                        index++;
                    }
                }
                else if (!repeat) {
                    if (auto lineGeometry = std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertices : lineGeometry->getVerticesList()) {
                            textProcessor(localId, labelId, groupId, std::optional<vt::TileLayerBuilder::Vertex>(), vertices, text, placementPriority, minimumDistance, allowOverlapSameFeatureId, sameFeatureIdDependent, 0);
                        }
                    }
                    else if (auto polygonGeometry = std::get_if<PolygonGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertex : polygonGeometry->getSurfacePoints()) {
                            textProcessor(localId, labelId, groupId, vertex, vt::TileLayerBuilder::Vertices(), text, placementPriority, minimumDistance, allowOverlapSameFeatureId, sameFeatureIdDependent, 0);
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
                        // A run with no spacing is ONE run for the whole line, and it is the line
                        // that carries it. A repeat has no run, so spacing 0 falls through to
                        // generateLinePoints, which steps it once - at the middle, like a billboard.
                        if (spacing <= 0 && lineRun) {
                            textProcessor(localId, labelId, groupId, std::optional<vt::TileLayerBuilder::Vertex>(), vertices, text, placementPriority, minimumDistance, allowOverlapSameFeatureId, sameFeatureIdDependent, 0);
                            continue;
                        }

                        for (const auto& transformedPoints : generateLinePoints(vertices, spacing, textSize, tileSize)) {
                            for (const auto& vertex : transformedPoints.second) {
                                long long generatedLabelId = combineId(labelId, std::hash<vt::TileId>()(tileId) * 63 + counter);
                                // The line goes with the label only when a run is laid out on it:
                                // TileLayerBuilder tesselates it per label and a billboard never reads it.
                                textProcessor(localId, generatedLabelId, groupId, vertex, billboardRepeat ? vt::TileLayerBuilder::Vertices() : vertices, text, placementPriority, minimumDistance, allowOverlapSameFeatureId, sameFeatureIdDependent, 0);
                                counter++;
                            }
                        }
                    }
                }
            }
        };
    }
    bool TextSymbolizer::segmentIntersectRectangle(double a_rectangleMinX, double a_rectangleMinY, double a_rectangleMaxX, double a_rectangleMaxY,
                                                   double a_p1x, double a_p1y, double a_p2x, double a_p2y)
    {
        // Find min and max X for the segment
        double minX = a_p1x;
        double maxX = a_p2x;
        if(a_p1x > a_p2x) {
            minX = a_p2x;
            maxX = a_p1x;
        }

        // Find the intersection of the segment's and rectangle's x-projections
        if(maxX > a_rectangleMaxX) {
            maxX = a_rectangleMaxX;
        }

        if(minX < a_rectangleMinX) {
            minX = a_rectangleMinX;
        }

        if(minX > maxX) {// If their projections do not intersect return false
            return false;
        }

        // Find corresponding min and max Y for min and max X we found before

        double minY = a_p1y;
        double maxY = a_p2y;
        double dx = a_p2x - a_p1x;
        if(std::abs(dx) > 0.0000001) {
            double a = (a_p2y - a_p1y) / dx;
            double b = a_p1y - a * a_p1x;
            minY = a * minX + b;
            maxY = a * maxX + b;
        }

        if(minY > maxY) {
            double tmp = maxY;
            maxY = minY;
            minY = tmp;
        }

        // Find the intersection of the segment's and rectangle's y-projections
        if(maxY > a_rectangleMaxY) {
            maxY = a_rectangleMaxY;
        }

        if(minY < a_rectangleMinY) {
            minY = a_rectangleMinY;
        }

        if(minY > maxY) {// If Y-projections do not intersect return false
            return false;
        }
        return true;
    }
    std::vector<std::pair<float, vt::TileLayerBuilder::Vertices>> TextSymbolizer::generateLinePoints(const vt::TileLayerBuilder::Vertices& vertices, float spacing, float textSize, float tileSize, bool applyAngle) {
        std::vector<std::pair<float, vt::TileLayerBuilder::Vertices>> transformedPointList;

        // text-spacing 0 means ONE run for the WHOLE line, which is what the label path does with
        // it (it hands the whole vertex list over as a single label). Restarting the pen at the
        // middle of every segment instead is why the same style drew one label per line when it
        // was culled and one per bend when it was clipped.
        float totalLength = 0;
        for (std::size_t i = 1; i < vertices.size(); i++) {
            totalLength += cglib::length(vertices[i] - vertices[i - 1]) * tileSize;
        }
        float step = (spacing > 0 ? spacing : totalLength) + textSize;
        float linePos = std::min(totalLength, step) * 0.5f;
        for (std::size_t i = 1; i < vertices.size(); i++) {
            const cglib::vec2<float>& v0 = vertices[i - 1];
            const cglib::vec2<float>& v1 = vertices[i];
            float lineLen = cglib::length(v1 - v0) * tileSize;
            // A segment outside the tile carries no anchor, but the pen still walks it: consuming
            // its length is what keeps the spacing constant ALONG THE LINE rather than restarting
            // it at every tile border.
            if (!segmentIntersectRectangle(0,0,1,1, v0(0), v0(1), v1(0), v1(1))) {
                linePos -= lineLen;
                continue;
            }

            vt::TileLayerBuilder::Vertices points;
            while (linePos < lineLen) {
                cglib::vec2<float> pos = v0 + (v1 - v0) * (linePos / lineLen);
                if (std::min(pos(0), pos(1)) > 0.0f && std::max(pos(0), pos(1)) < 1.0f) {
                    points.push_back(pos);
                }
                linePos += step;
            }
            if (!points.empty()) {
                if (applyAngle) {
                    cglib::vec2<float> dir = cglib::unit(v1 - v0);
                    float angle = std::atan2(-dir(1), dir(0));
                    transformedPointList.emplace_back(angle * 180.0f / boost::math::constants::pi<float>(), std::move(points));
                } else {
                    transformedPointList.emplace_back(0.0f, std::move(points));
                }
                
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
        // CALLOUT keeps its placement: the rotation is applied to the glyph quads by the style
        // transform like any other billboard, and a rotated callout (the peak-finder look) is a
        // normal thing to ask for.
        if (placement != vt::LabelOrientation::LINE && placement != vt::LabelOrientation::CALLOUT) {
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

        // A face name is a CSS-like list of its own ("Roboto, Helvetica Neue"), so a single
        // 'face-name' and a font set of several both end up as one chain of names
        std::vector<std::string> faceNames;
        if (!faceName.empty()) {
            faceNames = vt::parseFontNames(faceName);
        }
        else if (!fontSetName.empty()) {
            for (const std::shared_ptr<FontSet>& fontSet : _fontSets) {
                if (fontSet->getName() == fontSetName) {
                    for (const StringProperty& faceNameProp : fontSet->getFaceNames()) {
                        std::vector<std::string> names = vt::parseFontNames(faceNameProp.getValue(exprContext));
                        faceNames.insert(faceNames.end(), names.begin(), names.end());
                    }
                    break;
                }
            }
        }

        // Built from the back, so the first name that resolves becomes the main font and the ones
        // after it its glyph fallbacks. An unresolved name is skipped, and a list where nothing
        // resolves keeps the fallback font.
        for (auto it = faceNames.rbegin(); it != faceNames.rend(); it++) {
            if (std::shared_ptr<const vt::Font> mainFont = symbolizerContext.getFontManager()->getFont(*it, font)) {
                font = mainFont;
            }
        }

        // Rasterize the glyphs at a size that covers the label instead of magnifying one raster to
        // every size, which is what left large text soft (tangram's s_fontRasterSizes ladder,
        // core/src/text/fontContext.cpp). The style keeps the last word: a face named
        // 'face?glyph_size=N' is handed back untouched.
        const SymbolizerContext::Settings& settings = symbolizerContext.getSettings();
        float emSizePixels = _size.getStaticValue(exprContext) * settings.getFontScale() * settings.getPixelScale();
        if (std::shared_ptr<const vt::Font> sizedFont = symbolizerContext.getFontManager()->getFont(font, vt::pickGlyphRenderSize(emSizePixels))) {
            font = sizedFont;
        }
        return font;
    }

    vt::LabelPlateStyle TextSymbolizer::getPlateStyle(const SymbolizerContext& symbolizerContext, const ExpressionContext& exprContext) const {
        float fontScale = symbolizerContext.getSettings().getFontScale();
        vt::LabelPlateStyle plate;
        plate.color = vt::Color::fromColorOpacity(_backgroundFill.getValue(exprContext), _backgroundOpacity.getValue(exprContext));
        plate.radius = _backgroundRadius.getValue(exprContext) * fontScale;
        plate.padding = cglib::vec2<float>(_backgroundPaddingX.getValue(exprContext) * fontScale, _backgroundPaddingY.getValue(exprContext) * fontScale);
        plate.borderColor = vt::Color::fromColorOpacity(_backgroundBorderFill.getValue(exprContext), _backgroundBorderOpacity.getValue(exprContext));
        plate.borderWidth = _backgroundBorderWidth.getValue(exprContext) * fontScale;
        return plate;
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
        // The second run of text, if the style asks for one - the formatter lays it out with the
        // first one so that the pair share a baseline, a bounding box and a background plate.
        std::string secondaryText = _secondaryText.getValue(exprContext);
        float secondaryScale = _secondaryScale.getValue(exprContext);
        float secondaryGap = _secondaryDx.getValue(exprContext) * fontScale;
        float secondaryOffset = -_secondaryDy.getValue(exprContext) * fontScale;
        // NOT scaled by fontScale, unlike the offsets above: splitLines accumulates a word's width
        // from advances taken at the STYLE size (glyph.advance * _fontSize), so a threshold in
        // device pixels made every label wrap fontScale times too late - on a 2.6x screen, never.
        // Where a label wraps is a property of the style, not of the display.
        return vt::TextFormatter::Options(alignment, offset, wrapCharacter, wrapBefore, wrapWidth, characterSpacing, lineSpacing, secondaryText, secondaryScale, secondaryGap, secondaryOffset);
    }
}

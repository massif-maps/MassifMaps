#include "TileLayerBuilder.h"

#include <array>

#include <map>
#include <mutex>
#include <cmath>
#include <limits>
#include "TextFormatter.h"
#include "Color.h"

#include <cmath>
#include <utility>
#include <algorithm>
#include <iterator>

#include <boost/math/constants/constants.hpp>

namespace {
    // Triangles in the fan of a round line join - tangram's PolyLineBuilder value (JoinTypes::round,
    // core/src/util/builders.h). Each one costs a vertex and an index triple per join.
    static const std::size_t ROUND_JOIN_TRIANGLES = 5;
    // Below this turn (about 10 degrees) a round join is drawn as a plain miter. Tangram fans at
    // every angle, but their line shader has no antialias ramp: here five near-degenerate slivers
    // each carry their own ramp, and the ramps cut a hairline seam across the line.
    static const float ROUND_JOIN_DOT_LIMIT = 0.985f;

    // The pen walk Label::buildPointVertexData does. 'textPart' selects which half of the run is
    // measured: the icon glyphs come first, and the first CR pseudo-glyph resets the pen onto the
    // text's own origin.
    static cglib::bbox2<float> measureGlyphRun(const std::vector<massif::vt::Font::Glyph>& glyphs, bool textPart) {
        cglib::bbox2<float> bbox = cglib::bbox2<float>::smallest();
        cglib::vec2<float> pen(0, 0);
        bool text = false;
        for (const massif::vt::Font::Glyph& glyph : glyphs) {
            if (glyph.codePoint == massif::vt::Font::CR_CODEPOINT) {
                pen = cglib::vec2<float>(0, 0);
                text = true;
            }
            else if (text == textPart) {
                bbox.add(pen + glyph.offset);
                bbox.add(pen + glyph.offset + glyph.size);
            }
            pen += glyph.advance;
        }
        return bbox;
    }

    // Justification of the lines of a wrapped name on a given side. AUTO follows the side; an
    // explicit value is mirrored on the left, so "flush against the icon" means the same on both.
    static float resolveLineAlign(massif::vt::LabelLineAlign align, const cglib::vec2<float>& dir) {
        if (align == massif::vt::LabelLineAlign::AUTO) {
            return (dir(0) > 0 ? -1.0f : dir(0) < 0 ? 1.0f : 0.0f);
        }
        float base = (align == massif::vt::LabelLineAlign::LEFT ? -1.0f : align == massif::vt::LabelLineAlign::RIGHT ? 1.0f : 0.0f);
        return (dir(0) < 0 ? -base : base);
    }

    // One text layout per side the style allows (see TextLabelStyle::anchors). The glyph run is the
    // same every time, only its pen origin moves, so a side costs one vec2.
    //
    // Along the side's own axis the text is placed against the icon's edge, and dx/dy are re-applied
    // as a gap - pushed AWAY from the icon on either side. Across it the text is centred on the
    // anchor: a name above the icon has to be centred over it, and the formatter's own alignment is
    // derived from the sign of dx, which means nothing once dx is a gap.
    static std::vector<massif::vt::TileLabel::Variant> buildLabelVariants(const std::vector<massif::vt::LabelAnchor>& anchors, massif::vt::LabelLineAlign lineAlign, bool textOptional, bool hasIcon, const std::vector<massif::vt::Font::Glyph>& glyphs, const cglib::vec2<float>& iconExtent, const cglib::vec2<float>& styleOffset) {
        std::vector<massif::vt::TileLabel::Variant> variants;
        if (anchors.empty()) {
            return variants;
        }

        cglib::bbox2<float> textBBox = measureGlyphRun(glyphs, true);
        if (textBBox.min(0) > textBBox.max(0)) {
            return variants; // no text to move
        }
        // The box as it would be with no dx/dy, so that the offset can be re-applied per side.
        cglib::vec2<float> boxMin = textBBox.min - styleOffset;
        cglib::vec2<float> boxMax = textBBox.max - styleOffset;

        variants.reserve(anchors.size() + 1);
        for (massif::vt::LabelAnchor anchor : anchors) {
            cglib::vec2<float> dir = massif::vt::labelAnchorDirection(anchor);
            cglib::vec2<float> desired(0, 0);
            for (int i = 0; i < 2; i++) {
                float gap = std::abs(styleOffset(i));
                if (dir(i) > 0) {
                    desired(i) = iconExtent(i) - boxMin(i) + gap;
                }
                else if (dir(i) < 0) {
                    desired(i) = -iconExtent(i) - boxMax(i) - gap;
                }
                else {
                    desired(i) = -(boxMin(i) + boxMax(i)) * 0.5f + styleOffset(i);
                }
            }
            variants.emplace_back(desired - styleOffset, true, resolveLineAlign(lineAlign, dir));
        }
        if (textOptional && hasIcon) {
            variants.emplace_back(cglib::vec2<float>(0, 0), false);
        }
        return variants;
    }

    static float calculateScale(const massif::vt::VertexArray<float>& values, const massif::vt::VertexArray<std::size_t>& indices) {
        float maxValue = 0.0f;
        if (!values.empty()) {
            for (std::size_t index : indices) {
                float value = values[index];
                maxValue = std::max(maxValue, std::abs(value));
            }
        }
        if (maxValue == 0.0f) {
            return 1.0f;
        }
        return std::pow(2.0f, std::floor(std::log(32767.0f / maxValue) / std::log(2.0f)));
    }

    template <typename T>
    static float calculateScale(const massif::vt::VertexArray<T>& values, const massif::vt::VertexArray<std::size_t>& indices) {
        float maxValue = 0.0f;
        if (!values.empty()) {
            for (std::size_t index : indices) {
                const T& value = values[index];
                for (auto it = value.cbegin(); it != value.cend(); it++) {
                    maxValue = std::max(maxValue, std::abs(static_cast<float>(*it)));
                }
            }
        }
        if (maxValue == 0.0f) {
            return 1.0f;
        }
        return std::pow(2.0f, std::floor(std::log(32767.0f / maxValue) / std::log(2.0f)));
    }
}

namespace {
    // A white square with rounded corners, antialiased, used as the atlas cell a label's
    // background plate is 3-sliced from. Cached per radius: the glyph map dedupes by bitmap
    // POINTER, so handing it a fresh instance every time would add a cell per style rebuild.
    std::shared_ptr<const massif::vt::Bitmap> buildRoundedRectBitmap(int radius) {
        static std::mutex mutex;
        static std::map<int, std::shared_ptr<const massif::vt::Bitmap>> cache;
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cache.find(radius);
        if (it != cache.end()) {
            return it->second;
        }

        int size = std::max(4, radius * 2 + 2);
        std::vector<std::uint32_t> data(static_cast<std::size_t>(size) * size, 0xffffffffU);
        float r = static_cast<float>(radius);
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                // Distance outside the rounded rectangle, in pixels, at the nearest corner.
                float dx = std::max(0.0f, r - 0.5f - std::min(static_cast<float>(x), static_cast<float>(size - 1 - x)));
                float dy = std::max(0.0f, r - 0.5f - std::min(static_cast<float>(y), static_cast<float>(size - 1 - y)));
                float d = std::sqrt(dx * dx + dy * dy) - r + 0.5f;
                float alpha = std::min(1.0f, std::max(0.0f, 0.5f - d));
                std::uint32_t a = static_cast<std::uint32_t>(alpha * 255.0f + 0.5f);
                data[static_cast<std::size_t>(y) * size + x] = (a << 24) | (a << 16) | (a << 8) | a; // premultiplied white
            }
        }
        auto bitmap = std::make_shared<massif::vt::Bitmap>(size, size, std::move(data));
        cache[radius] = bitmap;
        return bitmap;
    }
}

namespace massif::vt {
    TileLayerBuilder::TileLayerBuilder(std::string layerName, int layerIdx, const TileId& tileId, const std::shared_ptr<const TileTransformer>& transformer, float tileSize, float geomScale) :
        _layerName(std::move(layerName)), _layerIdx(layerIdx), _tileId(tileId), _transformer(transformer->createTileVertexTransformer(tileId)), _tileSize(tileSize), _geomScale(geomScale)
    {
        _coords.reserve(RESERVED_VERTICES);
        _texCoords.reserve(RESERVED_VERTICES);
        _binormals.reserve(RESERVED_VERTICES);
        _heights.reserve(RESERVED_VERTICES);
        _attribs.reserve(RESERVED_VERTICES);
        _indices.reserve(RESERVED_VERTICES);
        _ids.reserve(RESERVED_VERTICES);
        _geoPosIndexes.reserve(RESERVED_VERTICES);
    }

    void TileLayerBuilder::setCompOp(std::optional<CompOp> compOp) {
        _compOp = std::move(compOp);
    }

    void TileLayerBuilder::setOpacityFunc(FloatFunction opacityFunc) {
        _opacityFunc = std::move(opacityFunc);
    }

    void TileLayerBuilder::setClipBox(const cglib::bbox2<float>& clipBox) {
        _clipBox = clipBox;
    }

    void TileLayerBuilder::setPolygonClipBox(const cglib::bbox2<float>& clipBox) {
        _polygonClipBox = clipBox;
    }

    void TileLayerBuilder::addBackground(const std::shared_ptr<TileBackground>& background) {
        _backgroundList.push_back(background);
    }

    void TileLayerBuilder::addBitmap(const std::shared_ptr<TileBitmap>& bitmap) {
        _bitmapList.push_back(bitmap);
    }

    TileLayerBuilder::PointProcessor TileLayerBuilder::createPointProcessor(const PointStyle& style, const std::shared_ptr<GlyphMap>& glyphMap) {
        if (style.sizeFunc == FloatFunction(0) || !style.image) {
            return PointProcessor();
        }

        cglib::vec2<float> translate(0, 0);
        std::optional<cglib::mat2x2<float>> transform;
        if (style.transform) {
            translate = style.transform->translate();
            if (auto matrix2 = style.transform->matrix2(); matrix2 != cglib::mat2x2<float>::identity()) {
                transform = matrix2;
            }
        }

        if (_builderParameters.type != TileGeometry::Type::POINT || _builderParameters.glyphMap != glyphMap || _builderParameters.translate != translate || _builderParameters.compOp != style.compOp || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        _builderParameters.type = TileGeometry::Type::POINT;
        _builderParameters.glyphMap = glyphMap;
        _builderParameters.translate = translate;
        _builderParameters.compOp = style.compOp;
        GlyphMap::GlyphId glyphId = glyphMap->loadBitmapGlyph(style.image->bitmap, GlyphMap::GlyphMode::BITMAP);
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc && _builderParameters.widthFuncs[styleIndex] == style.sizeFunc && _builderParameters.offsetFuncs[styleIndex] == FloatFunction(0)) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
            _builderParameters.widthFuncs[styleIndex] = style.sizeFunc;
            _builderParameters.offsetFuncs[styleIndex] = FloatFunction(0);
        }

        return [style, transform, styleIndex, glyphMap, glyphId, this](long long id, const Vertex& vertex) {
            std::size_t i0 = _coords.size();
            cglib::vec2<float> pen(0, 0);
            const GlyphMap::Glyph* glyph = glyphMap->getGlyph(glyphId);
            if (glyph) {
                pen = -cglib::vec2<float>(glyph->width, glyph->height) * 0.5f;
                tesselateGlyph(vertex, static_cast<std::int8_t>(styleIndex), pen * style.image->scale, cglib::vec2<float>(glyph->width, glyph->height) * style.image->scale, glyph);
            }
            _ids.fill(id, _indices.size() - _ids.size());
            _geoPosIndexes.fill(0, _indices.size() - _geoPosIndexes.size());
            if (transform) {
                for (std::size_t i = i0; i < _binormals.size(); i++) {
                    _binormals[i] = cglib::transform(_binormals[i], *transform);
                }
            }
        };
    }

    TileLayerBuilder::TextProcessor TileLayerBuilder::createTextProcessor(const TextStyle& style, const TextFormatter& formatter) {
        if (style.sizeFunc == FloatFunction(0) && !style.backgroundImage) {
            return TextProcessor();
        }

        cglib::vec2<float> translate(0, 0);
        std::optional<cglib::mat2x2<float>> transform;
        if (style.angle != 0) {
            float angle = -style.angle * boost::math::constants::pi<float>() / 180.0f;
            transform = cglib::rotate2_matrix(angle);
        }

        const std::shared_ptr<const Font>& font = formatter.getFont();
        int fontGlyphRenderSize = font->getGlyphRenderSize();

        bool needsNewBatch = _builderParameters.type != TileGeometry::Type::POINT 
            || _builderParameters.glyphMap != font->getGlyphMap() 
            || _builderParameters.glyphRenderSize != fontGlyphRenderSize 
            || _builderParameters.translate != translate 
            || _builderParameters.compOp != style.compOp 
            || _builderParameters.parameterCount + 2 > TileGeometry::StyleParameters::MAX_PARAMETERS;
        
        if (needsNewBatch) {
            appendGeometry();
        }
        _builderParameters.type = TileGeometry::Type::POINT;
        _builderParameters.glyphMap = font->getGlyphMap();
        _builderParameters.glyphRenderSize = fontGlyphRenderSize;
        _builderParameters.translate = translate;
        _builderParameters.compOp = style.compOp;
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc && _builderParameters.widthFuncs[styleIndex] == style.sizeFunc && _builderParameters.offsetFuncs[styleIndex] == FloatFunction(0)) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
            _builderParameters.widthFuncs[styleIndex] = style.sizeFunc;
            _builderParameters.offsetFuncs[styleIndex] = FloatFunction(0);
        }

        int haloStyleIndex = -1;
        if (style.haloRadiusFunc != FloatFunction(0)) {
            for (haloStyleIndex = _builderParameters.parameterCount; --haloStyleIndex >= 0; ) {
                if (_builderParameters.colorFuncs[haloStyleIndex] == style.haloColorFunc && _builderParameters.widthFuncs[haloStyleIndex] == style.sizeFunc && _builderParameters.offsetFuncs[haloStyleIndex] == style.haloRadiusFunc) {
                    break;
                }
            }
            if (haloStyleIndex < 0) {
                haloStyleIndex = _builderParameters.parameterCount++;
                _builderParameters.colorFuncs[haloStyleIndex] = style.haloColorFunc;
                _builderParameters.widthFuncs[haloStyleIndex] = style.sizeFunc;
                _builderParameters.offsetFuncs[haloStyleIndex] = style.haloRadiusFunc;
            }
        }

        return [style, transform, styleIndex, haloStyleIndex, font, formatter, this](long long id, const Vertex& vertex, const std::string& text, int geoPosIndex) {
            std::size_t i0 = _coords.size();
            std::vector<Font::Glyph> glyphs = formatter.format(text, 1.0f);
            Font::Metrics metrics = font->getMetrics(1.0f);
            if (style.backgroundImage) {
                const GlyphMap::Glyph* baseGlyph = font->getGlyphMap()->getGlyph(font->getGlyphMap()->loadBitmapGlyph(style.backgroundImage->bitmap, GlyphMap::GlyphMode::BACKGROUND));
                if (baseGlyph) {
                    float scale = style.backgroundImage->scale / formatter.getFontSize();
                    Font::Glyph glyph(0, Font::NULL_CODEPOINT, *baseGlyph, cglib::vec2<float>(baseGlyph->width, baseGlyph->height) * (style.backgroundScale * scale), style.backgroundOffset * scale, cglib::vec2<float>(0, 0));
                    tesselateGlyph(vertex, styleIndex, glyph.offset * style.backgroundImage->scale, glyph.size * style.backgroundImage->scale, &glyph.baseGlyph);
                }
            }

            for (int pass = (haloStyleIndex >= 0 ? 0 : 1); pass < 2; pass++) {
                cglib::vec2<float> pen(0, 0);
                for (Font::Glyph& glyph : glyphs) {
                    if (glyph.codePoint == Font::CR_CODEPOINT) {
                        pen = cglib::vec2<float>(0, 0);
                    }
                    else {
                        cglib::vec2<float> offset(glyph.offset(0), metrics.ascent + metrics.descent - glyph.size(1) - glyph.offset(1));
                        tesselateGlyph(vertex, static_cast<std::int8_t>(pass == 0 ? haloStyleIndex : styleIndex), pen + offset, glyph.size, &glyph.baseGlyph);
                    }

                    pen += glyph.advance;
                }
            }
            _ids.fill(id, _indices.size() - _ids.size());
            _geoPosIndexes.fill(geoPosIndex, _indices.size() - _geoPosIndexes.size());
            if (transform) {
                for (std::size_t i = i0; i < _binormals.size(); i++) {
                    _binormals[i] = cglib::transform(_binormals[i], *transform);
                }
            }
        };
    }

    TileLayerBuilder::LineProcessor TileLayerBuilder::createLineProcessor(const LineStyle& style, const std::shared_ptr<StrokeMap>& strokeMap) {
        if (style.widthFunc == FloatFunction(0)) {
            return LineProcessor();
        }

        cglib::vec2<float> translate(0, 0);
        std::optional<cglib::mat2x2<float>> transform;
        std::optional<cglib::mat2x2<float>> invTransTransform;
        if (style.transform) {
            translate = style.transform->translate();
            if (auto matrix2 = style.transform->matrix2(); matrix2 != cglib::mat2x2<float>::identity()) {
                transform = matrix2;
                invTransTransform = cglib::transpose(cglib::inverse(matrix2));
            }
        }

        if ((_builderParameters.strokeMap && _builderParameters.strokeMap != strokeMap) || _builderParameters.translate != translate || _builderParameters.compOp != style.compOp || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        else if (!(_builderParameters.type == TileGeometry::Type::LINE || (_builderParameters.type == TileGeometry::Type::POLYGON && !_builderParameters.pattern))) { // we can use also line drawing shader but ONLY if pattern is not used for polygons (pattern can be used for lines)
            appendGeometry();
        }
        _builderParameters.type = TileGeometry::Type::LINE;
        _builderParameters.strokeMap = strokeMap;
        _builderParameters.translate = translate;
        _builderParameters.compOp = style.compOp;
        StrokeMap::StrokeId strokeId = (style.strokePattern ? strokeMap->loadBitmapPattern(style.strokePattern) : 0);
        const StrokeMap::Stroke* stroke = (strokeId != 0 ? strokeMap->getStroke(strokeId) : nullptr);
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc && _builderParameters.widthFuncs[styleIndex] == style.widthFunc && _builderParameters.offsetFuncs[styleIndex] == style.offsetFunc && _builderParameters.lineStrokeIds[styleIndex] == strokeId) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
            _builderParameters.widthFuncs[styleIndex] = style.widthFunc;
            _builderParameters.offsetFuncs[styleIndex] = style.offsetFunc;
            _builderParameters.lineStrokeIds[styleIndex] = strokeId;
        }
        registerStyleVariantSlot(styleIndex);

        return [style, transform, invTransTransform, styleIndex, stroke, this](long long id, const Vertices& vertices) {
            std::size_t i0 = _coords.size();
            _binormals.fill(cglib::vec2<float>(0, 0), _coords.size() - _binormals.size()); // needed if previously only polygons were used
            tesselateLine(vertices, static_cast<std::int8_t>(styleIndex), stroke, style);
            _ids.fill(id, _indices.size() - _ids.size());
            _geoPosIndexes.fill(0, _indices.size() - _geoPosIndexes.size());
            if (transform) {
                for (std::size_t i = i0; i < _coords.size(); i++) {
                    _coords[i] = cglib::transform(_coords[i], *transform);
                }
                for (std::size_t i = i0; i < _binormals.size(); i++) {
                    _binormals[i] = cglib::unit(cglib::transform(_binormals[i], *invTransTransform)) * cglib::length(_binormals[i]);
                }
            }
        };
    }

    TileLayerBuilder::PolygonProcessor TileLayerBuilder::createPolygonProcessor(const PolygonStyle& style) {
        cglib::vec2<float> translate(0, 0);
        std::optional<cglib::mat2x2<float>> transform;
        if (style.transform) {
            translate = style.transform->translate();
            if (auto matrix2 = style.transform->matrix2(); matrix2 != cglib::mat2x2<float>::identity()) {
                transform = matrix2;
            }
        }

        TileGeometry::Type type = TileGeometry::Type::POLYGON;

        // Only a SECOND, different pattern splits the geometry. A plain fill joins a patterned one
        // (and the other way round) on a per-slot flag, because alternating the two was 48% of
        // every geometry draw in a city frame - see docs/internals/rendering/10-performance.md.
        bool patternConflict = style.pattern && _builderParameters.pattern && _builderParameters.pattern != style.pattern;
        if (patternConflict || _builderParameters.translate != translate || _builderParameters.compOp != style.compOp || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        else if (!(_builderParameters.type == TileGeometry::Type::POLYGON || (_builderParameters.type == TileGeometry::Type::LINE && !style.pattern))) { // we can use also line drawing shader but ONLY if pattern is not used for polygons (pattern can be used for lines)
            appendGeometry();
        }
        else {
            type = _builderParameters.type;
        }
        _builderParameters.type = type;
        // Sticky: a plain fill must not clear the pattern the geometry already carries.
        if (style.pattern || !_builderParameters.pattern) {
            _builderParameters.pattern = style.pattern;
        }
        _builderParameters.translate = translate;
        _builderParameters.compOp = style.compOp;
        bool patternUsed = static_cast<bool>(style.pattern);
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc && _builderParameters.widthFuncs[styleIndex] == FloatFunction(0) && _builderParameters.offsetFuncs[styleIndex] == FloatFunction(0) && _builderParameters.lineStrokeIds[styleIndex] == 0 && _builderParameters.patternUsed[styleIndex] == patternUsed) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
            _builderParameters.widthFuncs[styleIndex] = FloatFunction(0); // fill width information when we need to use line shader with polygons
            _builderParameters.offsetFuncs[styleIndex] = FloatFunction(0); // fill offset information when we need to use line shader with polygons
            _builderParameters.lineStrokeIds[styleIndex] = 0; // fill stroke information when we need to use line shader with polygons
            _builderParameters.patternUsed[styleIndex] = patternUsed;
        }

        return [type, style, transform, styleIndex, this](long long id, const VerticesList& verticesList) {
            std::size_t i0 = _coords.size();
            tesselatePolygon(verticesList, static_cast<std::int8_t>(styleIndex), style);
            _ids.fill(id, _indices.size() - _ids.size());
            _geoPosIndexes.fill(0, _indices.size() - _geoPosIndexes.size());
            if (type == TileGeometry::Type::LINE) {
                _binormals.fill(cglib::vec2<float>(0, 0), _coords.size() - _binormals.size()); // use zero binormals if using 'lines'
            }
            if (transform) {
                for (std::size_t i = i0; i < _coords.size(); i++) {
                    _coords[i] = cglib::transform(_coords[i], *transform);
                }
            }
        };
    }

    TileLayerBuilder::Polygon3DProcessor TileLayerBuilder::createPolygon3DProcessor(const Polygon3DStyle& style) {
        cglib::vec2<float> translate(0, 0);
        std::optional<cglib::mat2x2<float>> transform;
        std::optional<cglib::mat2x2<float>> invTransTransform;
        if (style.transform) {
            translate = style.transform->translate();
            if (auto matrix2 = style.transform->matrix2(); matrix2 != cglib::mat2x2<float>::identity()) {
                transform = matrix2;
                invTransTransform = cglib::transpose(cglib::inverse(matrix2));
            }
        }

        if (_builderParameters.type != TileGeometry::Type::POLYGON3D || _builderParameters.translate != translate || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        _builderParameters.type = TileGeometry::Type::POLYGON3D;
        _builderParameters.translate = translate;
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
        }

        return [style, transform, invTransTransform, styleIndex, this](long long id, const VerticesList& verticesList, float minHeight, float maxHeight) {
            std::size_t i0 = _coords.size();
            tesselatePolygon3D(verticesList, minHeight, maxHeight, static_cast<std::int8_t>(styleIndex), style);
            // The footprint's reach, over every vertex this feature produced. Stamped here rather
            // than at each append: the vertex stage needs it on all of them, and the tesselator has
            // half a dozen paths that emit one.
            for (std::size_t i = i0; i < _attribs.size(); i++) {
                _attribs[i](2) = _polygon3DExtent;
            }
            _ids.fill(id, _indices.size() - _ids.size());
            _groundIds.fill(id, _groundIndices.size() - _groundIds.size());
            _geoPosIndexes.fill(0, _indices.size() - _geoPosIndexes.size());
            if (transform) {
                for (std::size_t i = i0; i < _coords.size(); i++) {
                    _coords[i] = cglib::transform(_coords[i], *transform);
                }
                for (std::size_t i = i0; i < _binormals.size(); i++) {
                    _binormals[i] = cglib::unit(cglib::transform(_binormals[i], *invTransTransform)) * cglib::length(_binormals[i]);
                }
                for (std::size_t i = i0; i < _texCoords.size(); i++) {
                    _texCoords[i] = cglib::transform(_texCoords[i], *transform);
                }
            }
        };
    }

    TileLayerBuilder::PointLabelProcessor TileLayerBuilder::createPointLabelProcessor(const PointLabelStyle& style, const std::shared_ptr<GlyphMap>& glyphMap) {
        if (style.sizeFunc == FloatFunction(0) || !style.image) {
            return PointLabelProcessor();
        }

        const GlyphMap::Glyph* baseGlyph = glyphMap->getGlyph(glyphMap->loadBitmapGlyph(style.image->bitmap, GlyphMap::GlyphMode::BITMAP));
        if (!baseGlyph) {
            return PointLabelProcessor();
        }
        std::vector<Font::Glyph> bitmapGlyphs = {
            Font::Glyph(0, Font::CR_CODEPOINT, GlyphMap::Glyph(GlyphMap::GlyphMode::BACKGROUND, 0, 0, 0, 0, cglib::vec2<float>(0, 0)), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), -cglib::vec2<float>(style.image->bitmap->width, style.image->bitmap->height) * (style.image->scale * 0.5f)),
            Font::Glyph(0, Font::NULL_CODEPOINT, *baseGlyph, cglib::vec2<float>(baseGlyph->width, baseGlyph->height) * style.image->scale, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0))
        };

        float scale = 1.0f / _tileSize;
        std::optional<Transform> transform;
        if (style.transform) {
            cglib::mat3x3<float> flippedTransform = style.transform->matrix3() * cglib::scale3_matrix(cglib::vec3<float>(1, -1, 1));
            cglib::mat2x2<float> matrix = { { flippedTransform(0, 0), flippedTransform(0, 1) }, { -flippedTransform(1, 0), -flippedTransform(1, 1) } };
            cglib::vec2<float> translate(flippedTransform(0, 2) / _tileSize, flippedTransform(1, 2) / _tileSize);
            transform = Transform::fromMatrix2Translate(matrix, translate);
        }

        if (!_labelStyle || _labelStyle->orientation != style.orientation || _labelStyle->colorFunc != style.colorFunc || _labelStyle->sizeFunc != style.sizeFunc || _labelStyle->haloColorFunc != ColorFunction() || _labelStyle->haloRadiusFunc != FloatFunction() || _labelStyle->autoflip != style.autoflip || _labelStyle->scale != scale || _labelStyle->ascent != 0.0f || _labelStyle->descent != 0.0f || _labelStyle->transform != transform || _labelStyle->glyphMap != glyphMap || _labelStyle->maxDistance != style.maxDistance || _labelStyle->occlusionOpacity != style.occlusionOpacity) {
            auto labelStyle = std::make_shared<TileLabel::Style>(style.orientation, style.colorFunc, style.sizeFunc, ColorFunction(), FloatFunction(), style.autoflip, scale, 0.0f, 0.0f, transform, glyphMap, 27, style.maxDistance);
            labelStyle->occlusionOpacity = style.occlusionOpacity; // not in the ctor: its signature is long enough
            _labelStyle = labelStyle;
        }

        return [bitmapGlyphs, this](long long id, long long labelId, long long groupId, const std::variant<Vertex, Vertices>& position, float priority, float minimumGroupDistance, bool allowOverlapSameFeatureId, bool sameFeatureIdDependent, int geoPointIndex) {
            std::optional<cglib::vec2<float>> labelPosition;
            std::vector<cglib::vec2<float>> labelVertices;
            if (auto pos = std::get_if<Vertex>(&position)) {
                labelPosition = *pos;
            }
            else if (auto vertices = std::get_if<Vertices>(&position)) {
                VertexArray<cglib::vec2<float>> tesselatedVertices;
                _transformer->tesselateLabelLineString(vertices->data(), vertices->size(), tesselatedVertices);
                labelVertices.assign(tesselatedVertices.begin(), tesselatedVertices.end());
            }

            TileLabel::PlacementInfo placementInfo(priority, minimumGroupDistance, allowOverlapSameFeatureId, sameFeatureIdDependent);
            long long globalId = (labelId ^ (static_cast<long long>(_layerIdx) << 32)) * 3 + 0;
            auto pointLabel = std::make_shared<TileLabel>(id, globalId, groupId, bitmapGlyphs, std::move(labelPosition), std::move(labelVertices), _labelStyle, placementInfo, geoPointIndex);
            _labelList.push_back(std::move(pointLabel));
        };
    }

    TileLayerBuilder::TextLabelProcessor TileLayerBuilder::createTextLabelProcessor(const TextLabelStyle& style, const TextFormatter& formatter) {
        if (style.sizeFunc == FloatFunction(0) && !style.backgroundImage && style.iconGlyphs.empty()) {
            return TextLabelProcessor();
        }

        float scale = 1.0f / _tileSize;
        std::optional<Transform> transform;
        if (style.orientation != LabelOrientation::LINE && style.angle != 0) {
            float angle = style.angle * boost::math::constants::pi<float>() / 180.0f;
            transform = Transform::fromMatrix2(cglib::rotate2_matrix(angle));
        }

        const std::shared_ptr<const Font>& font = formatter.getFont();
        Font::Metrics metrics = formatter.getFont()->getMetrics(1.0f);
        int glyphRenderSize = font->getGlyphRenderSize();
        
        bool needsNewLabelStyle = !_labelStyle 
            || _labelStyle->orientation != style.orientation 
            || _labelStyle->colorFunc != style.colorFunc 
            || _labelStyle->sizeFunc != style.sizeFunc 
            || _labelStyle->haloColorFunc != style.haloColorFunc 
            || _labelStyle->haloRadiusFunc != style.haloRadiusFunc 
            || _labelStyle->autoflip != style.autoflip 
            || _labelStyle->scale != scale 
            || _labelStyle->ascent != metrics.ascent 
            || _labelStyle->descent != metrics.descent 
            || _labelStyle->transform != transform 
            || _labelStyle->glyphMap != font->getGlyphMap() 
            || _labelStyle->glyphRenderSize != glyphRenderSize
            || _labelStyle->maxDistance != style.maxDistance
            || _labelStyle->occlusionOpacity != style.occlusionOpacity
            || _labelStyle->rankFunc != style.rankFunc
            || _labelStyle->secondaryColorFunc.has_value() != style.secondaryColorFunc.has_value()
            || (style.secondaryColorFunc && *_labelStyle->secondaryColorFunc != *style.secondaryColorFunc)
            || _labelStyle->calloutLineAnchor != style.calloutLineAnchor
            || _labelStyle->calloutBandAnchor != style.calloutBandAnchor
            || _labelStyle->calloutScreenAnchor != style.calloutScreenAnchor
            || _labelStyle->calloutOffset != style.calloutOffset
            || _labelStyle->calloutStep != style.calloutStep
            || _labelStyle->calloutMaxRows != style.calloutMaxRows
            || _labelStyle->calloutPersistPasses != style.calloutPersistPasses
            || _labelStyle->calloutLineWidth != style.calloutLineWidth
            || _labelStyle->textPlate.style != style.textPlate
            || _labelStyle->iconPlate.style != style.iconPlate
            || _labelStyle->textLineAlign != resolveLineAlign(style.textLineAlign, cglib::vec2<float>(0, 0));

        if (needsNewLabelStyle) {
            std::optional<GlyphMap::Glyph> calloutLineGlyph;
            if (style.orientation == LabelOrientation::CALLOUT && style.calloutLineWidth > 0) {
                // The leader line is drawn as one more glyph quad, so it needs an opaque cell in
                // the same atlas the text comes from - the quad is sized at draw time (the length
                // is the culler's, and it changes every frame), so only the cell is loaded here.
                // One shared instance: the glyph map dedupes by bitmap POINTER, so a new one per
                // style would add a cell to the atlas every time a style is rebuilt.
                static const std::shared_ptr<const Bitmap> whiteBitmap = std::make_shared<Bitmap>(4, 4, std::vector<std::uint32_t>(16, 0xffffffffU));
                if (const GlyphMap::Glyph* lineGlyph = font->getGlyphMap()->getGlyph(font->getGlyphMap()->loadBitmapGlyph(whiteBitmap, GlyphMap::GlyphMode::BITMAP))) {
                    calloutLineGlyph = *lineGlyph;
                }
            }
            // A plate is 3-sliced from one atlas cell: a rounded-corner square whose left and right
            // halves are the caps and whose middle column is stretched. The bitmaps are cached by
            // radius (the glyph map dedupes by POINTER), so a style that rebuilds does not grow the
            // atlas - and a border is the same cell at radius + borderWidth, drawn behind the fill.
            auto resolvePlate = [&font](const LabelPlateStyle& plateStyle) {
                TileLabel::Style::Plate plate;
                plate.style = plateStyle;
                auto loadCell = [&font](float radius) -> std::optional<GlyphMap::Glyph> {
                    int cellRadius = std::min(32, std::max(0, static_cast<int>(radius + 0.5f)));
                    if (const GlyphMap::Glyph* glyph = font->getGlyphMap()->getGlyph(font->getGlyphMap()->loadBitmapGlyph(buildRoundedRectBitmap(cellRadius), GlyphMap::GlyphMode::BITMAP))) {
                        return *glyph;
                    }
                    return std::optional<GlyphMap::Glyph>();
                };
                if (plateStyle.hasFill()) {
                    plate.glyph = loadCell(plateStyle.radius);
                }
                if (plateStyle.hasBorder()) {
                    plate.borderGlyph = loadCell(plateStyle.radius + plateStyle.borderWidth);
                }
                return plate;
            };
            TileLabel::Style::Plate textPlate = resolvePlate(style.textPlate);
            TileLabel::Style::Plate iconPlate = resolvePlate(style.iconPlate);
            auto labelStyle = std::make_shared<TileLabel::Style>(style.orientation, style.colorFunc, style.sizeFunc, style.haloColorFunc, style.haloRadiusFunc, style.autoflip, scale, metrics.ascent, metrics.descent, transform, font->getGlyphMap(), glyphRenderSize, style.maxDistance, style.secondaryColorFunc, style.rankFunc, style.calloutScreenAnchor, style.calloutOffset, style.calloutStep, style.calloutMaxRows, style.calloutPersistPasses, style.calloutLineWidth, style.calloutLineAnchor, style.calloutBandAnchor, calloutLineGlyph, textPlate, iconPlate, resolveLineAlign(style.textLineAlign, cglib::vec2<float>(0, 0)), style.iconColorFunc);
            labelStyle->occlusionOpacity = style.occlusionOpacity; // not in the ctor: its signature is long enough
            _labelStyle = labelStyle;
        }

        // The glyphs that come before the text and stay on the anchor when the text moves: the
        // shield bitmap first, then the icon run the style shaped for us. Built once per style
        // rather than per label - neither depends on the feature's text.
        std::vector<Font::Glyph> iconGlyphs;
        if (style.backgroundImage) {
            const GlyphMap::Glyph* baseGlyph = font->getGlyphMap()->getGlyph(font->getGlyphMap()->loadBitmapGlyph(style.backgroundImage->bitmap, GlyphMap::GlyphMode::BACKGROUND));
            if (baseGlyph) {
                float imageScale = style.backgroundImage->scale / formatter.getFontSize();
                iconGlyphs.emplace_back(0, Font::NULL_CODEPOINT, *baseGlyph, cglib::vec2<float>(baseGlyph->width, baseGlyph->height) * (style.backgroundScale * imageScale), style.backgroundOffset * imageScale, cglib::vec2<float>(baseGlyph->width, 0) * imageScale);
            }
        }
        iconGlyphs.insert(iconGlyphs.end(), style.iconGlyphs.begin(), style.iconGlyphs.end());
        // What the anchored text has to clear: half of it on each side, like tangram's
        // (own + relative) * 0.5 (labelProperty.h). The shield bitmap is centred on the anchor by
        // its own offset, so this is measured around the anchor and not around the run's origin.
        cglib::vec2<float> iconExtent(0, 0);
        if (!style.anchors.empty() && !iconGlyphs.empty()) {
            cglib::bbox2<float> iconBBox = measureGlyphRun(iconGlyphs, false);
            if (iconBBox.min(0) <= iconBBox.max(0)) {
                iconExtent = cglib::vec2<float>(std::max(std::abs(iconBBox.min(0)), std::abs(iconBBox.max(0))),
                                                std::max(std::abs(iconBBox.min(1)), std::abs(iconBBox.max(1))));
            }
        }
        // dx/dy in the units the glyph run carries them (the formatter divides by the font size).
        float invFontSize = (formatter.getFontSize() != 0 ? 1.0f / formatter.getFontSize() : 0.0f);
        cglib::vec2<float> styleOffset = formatter.getOptions().offset * invFontSize;
        bool hasIcon = !iconGlyphs.empty();

        return [style, font, formatter, iconGlyphs, iconExtent, styleOffset, hasIcon, this](long long id, long long labelId, long long groupId, const std::optional<Vertex>& position, const Vertices& vertices, const std::string& text, float priority, float minimumGroupDistance, bool allowOverlapSameFeatureId, bool sameFeatureIdDependent, int geoPointIndex) {
            if (!text.empty() || !iconGlyphs.empty()) {
                std::vector<Font::Glyph> glyphs;
                if (!text.empty()) {
                    glyphs = formatter.format(text, 1.0f);
                }
                glyphs.insert(glyphs.begin(), iconGlyphs.begin(), iconGlyphs.end());

                std::vector<TileLabel::Variant> variants = buildLabelVariants(style.anchors, style.textLineAlign, style.textOptional, hasIcon, glyphs, iconExtent, styleOffset);

                std::optional<cglib::vec2<float>> labelPosition;
                if (position) {
                    labelPosition = *position;
                }
                std::vector<cglib::vec2<float>> labelVertices;
                if (!vertices.empty()) {
                    VertexArray<cglib::vec2<float>> tesselatedVertices;
                    _transformer->tesselateLabelLineString(vertices.data(), vertices.size(), tesselatedVertices);
                    labelVertices.assign(tesselatedVertices.begin(), tesselatedVertices.end());
                }

                TileLabel::PlacementInfo placementInfo(priority, minimumGroupDistance, allowOverlapSameFeatureId, sameFeatureIdDependent);
                long long globalId = (labelId ^ (static_cast<long long>(_layerIdx) << 32)) * 3 + (hasIcon ? 2 : 1);
                auto textLabel = std::make_shared<TileLabel>(id, globalId, groupId, std::move(glyphs), std::move(labelPosition), std::move(labelVertices), _labelStyle, placementInfo, geoPointIndex, std::move(variants));
                _labelList.push_back(std::move(textLabel));
            }
        };
    }

    std::shared_ptr<TileLayer> TileLayerBuilder::buildTileLayer() const {
        std::vector<std::shared_ptr<TileGeometry>> geometryList = _geometryList;
        packGeometry(geometryList);
        packGroundSkirt(geometryList);

        return std::make_shared<TileLayer>(_layerName, _layerIdx, _compOp, _opacityFunc, _backgroundList, _bitmapList, std::move(geometryList), _labelList);
    }

    void TileLayerBuilder::packGroundSkirt(std::vector<std::shared_ptr<TileGeometry>>& geometryList) const {
        if (_groundIndices.empty()) {
            return;
        }
        VertexArray<cglib::vec3<float>> coords;
        VertexArray<cglib::vec3<float>> normals;
        coords.reserve(_groundCoords.size());
        normals.reserve(_groundCoords.size());
        for (std::size_t i = 0; i < _groundCoords.size(); i++) {
            coords.append(_transformer->calculatePoint(_groundCoords[i]));
            normals.append(_transformer->calculateNormal(_groundCoords[i]));
        }
        VertexArray<float> heights;
        heights.reserve(_groundHeights.size());
        for (std::size_t i = 0; i < _groundHeights.size(); i++) {
            heights.append(_transformer->calculateHeight(_groundCoords[i], _groundHeights[i]));
        }

        // White, so the draw survives the all-transparent skip in renderTileGeometry: the darkening
        // itself is the fragment's distance to the footprint times the style's intensity, not a colour.
        TileGeometry::StyleParameters styleParameters;
        styleParameters.parameterCount = 1;
        styleParameters.colorFuncs[0] = ColorFunction(Color(1.0f, 1.0f, 1.0f, 1.0f));

        VertexArray<std::uint16_t> geoPosIndexes;
        // The scales are what the vertices are QUANTISED to on the way into int16, so they have to
        // be measured from the data exactly as the main path does. Passing 1 collapsed every skirt
        // vertex onto integer tile coordinates - one triangle per block instead of a contact
        // shadow, multiplied into the ground as a black wedge.
        float coordScale = calculateScale(coords, _groundIndices);
        float binormalScale = calculateScale(_groundBinormals, _groundIndices);
        float texCoordScale = calculateScale(_groundTexCoords, _groundIndices);
        float heightScale = calculateScale(heights, _groundIndices);

        // ...and the index buffer is UNSIGNED SHORT, so the same split and remap the main path does.
        // Without it a dense tile runs past 65535 skirt vertices, the indices wrap, and triangles
        // stitch unrelated vertices into slivers hundreds of metres long - dark bands raking across
        // the map wherever buildings are packed tightly.
        for (std::size_t offset = 0; offset < _groundIndices.size(); ) {
            std::size_t count = std::min(std::size_t(65535), _groundIndices.size() - offset);

            std::vector<std::size_t> indexTable(coords.size(), 65536);
            VertexArray<cglib::vec3<float>> remappedCoords;
            VertexArray<cglib::vec2<float>> remappedTexCoords;
            VertexArray<cglib::vec3<float>> remappedNormals;
            VertexArray<cglib::vec3<float>> remappedBinormals;
            VertexArray<float> remappedHeights;
            VertexArray<cglib::vec4<std::int8_t>> remappedAttribs;
            VertexArray<std::size_t> remappedIndices;
            VertexArray<long long> remappedIds;
            remappedIndices.reserve(count);
            remappedIds.reserve(count);
            for (std::size_t i = 0; i < count; i++) {
                std::size_t index = _groundIndices[offset + i];
                std::size_t remappedIndex = indexTable[index];
                if (remappedIndex == 65536) {
                    remappedIndex = remappedCoords.size();
                    indexTable[index] = remappedIndex;
                    remappedCoords.append(coords[index]);
                    remappedTexCoords.append(_groundTexCoords[index]);
                    remappedNormals.append(normals[index]);
                    remappedBinormals.append(_groundBinormals[index]);
                    remappedHeights.append(heights[index]);
                    remappedAttribs.append(_groundAttribs[index]);
                }
                remappedIndices.append(remappedIndex);
                remappedIds.append(offset + i < _groundIds.size() ? _groundIds[offset + i] : 0);
            }
            offset += count;

            packGeometry(TileGeometry::Type::POLYGON3DGROUND, 3, coordScale, binormalScale, texCoordScale, heightScale, remappedCoords, remappedTexCoords, remappedNormals, remappedBinormals, remappedHeights, remappedAttribs, remappedIndices, remappedIds, geoPosIndexes, styleParameters, std::vector<TileGeometry::FeatureStyleRange>(), geometryList);
        }
    }

    void TileLayerBuilder::beginStyleVariant(std::uint64_t stateKey) {
        // Both slots have to land in the SAME geometry as the vertices, so make room for them
        // before anything is registered - a flush in the middle of a variant would leave the second
        // slot indexing another geometry's style parameters.
        if (_builderParameters.parameterCount + 2 > TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        _styleVariantSlots.clear();
        _styleVariantFirstVertex = _coords.size();
        _styleVariantStateKey = stateKey;
        _styleVariantGeneration = _geometryGeneration;
    }

    void TileLayerBuilder::reserveInvisibleLineStyle() {
        // The branch that paints nothing still needs a slot to be repointed at: a zero width is
        // what makes the feature disappear when the parameter stops picking it.
        if (_styleVariantGeneration != _geometryGeneration || _builderParameters.type != TileGeometry::Type::LINE || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            return;
        }
        int styleIndex = _builderParameters.parameterCount++;
        _builderParameters.colorFuncs[styleIndex] = ColorFunction(Color());
        _builderParameters.widthFuncs[styleIndex] = FloatFunction(0);
        _builderParameters.offsetFuncs[styleIndex] = FloatFunction(0);
        _builderParameters.lineStrokeIds[styleIndex] = 0;
        registerStyleVariantSlot(styleIndex);
    }

    void TileLayerBuilder::registerStyleVariantSlot(int styleIndex) {
        if (_styleVariantGeneration == _geometryGeneration && _styleVariantSlots.size() < 2) {
            _styleVariantSlots.push_back(styleIndex);
        }
    }

    void TileLayerBuilder::endStyleVariant(int selectedSlot, bool selected) {
        std::size_t vertexCount = _coords.size() - _styleVariantFirstVertex;
        bool complete = _styleVariantGeneration == _geometryGeneration && _styleVariantSlots.size() == 2 && vertexCount > 0;
        _styleVariantGeneration = -1;
        if (!complete) {
            return; // nothing drawn, or the geometry was flushed underneath: not repointable
        }

        // The vertices carry the slot of whichever branch tesselated them, which is not the active
        // one when the active branch paints nothing.
        std::uint8_t styleIndices[2] = { static_cast<std::uint8_t>(_styleVariantSlots[selectedSlot == 0 ? 1 : 0]), static_cast<std::uint8_t>(_styleVariantSlots[selectedSlot]) };
        std::uint8_t styleIndex = styleIndices[selected ? 1 : 0];
        for (std::size_t i = _styleVariantFirstVertex; i < _coords.size() && i < _attribs.size(); i++) {
            _attribs[i](0) = static_cast<std::int8_t>(styleIndex);
        }
        _styleVariantRanges.push_back(StyleVariantRange { _styleVariantFirstVertex, vertexCount, _styleVariantStateKey, { styleIndices[0], styleIndices[1] } });
    }

    void TileLayerBuilder::appendGeometry() {
        if (_builderParameters.type == TileGeometry::Type::NONE) {
            return;
        }

        packGeometry(_geometryList);

        _geometryGeneration++;
        _builderParameters = BuilderParameters();
        _coords.clear();
        _texCoords.clear();
        _binormals.clear();
        _heights.clear();
        _attribs.clear();
        _indices.clear();
        _ids.clear();
        _geoPosIndexes.clear();
        _styleVariantRanges.clear();
    }

    void TileLayerBuilder::packGeometry(std::vector<std::shared_ptr<TileGeometry>>& geometryList) const {
        if (_builderParameters.type == TileGeometry::Type::NONE) {
            return;
        }

        // Create style parameters
        TileGeometry::StyleParameters styleParameters;
        styleParameters.parameterCount = _builderParameters.parameterCount;
        for (int i = 0; i < styleParameters.parameterCount; i++) {
            styleParameters.colorFuncs[i] = _builderParameters.colorFuncs[i];
            styleParameters.widthFuncs[i] = _builderParameters.widthFuncs[i];
            styleParameters.offsetFuncs[i] = _builderParameters.offsetFuncs[i];
            const StrokeMap::Stroke* stroke = nullptr;
            if (_builderParameters.strokeMap && _builderParameters.lineStrokeIds[i] != 0) {
                stroke = _builderParameters.strokeMap->getStroke(_builderParameters.lineStrokeIds[i]);
            }
            styleParameters.strokeScales[i] = (stroke ? stroke->scale : 0);
            styleParameters.patternScales[i] = (_builderParameters.patternUsed[i] ? 1.0f : 0.0f);
        }
        if (_builderParameters.translate != cglib::vec2<float>(0, 0)) {
            styleParameters.translate = _builderParameters.translate * (1.0f / _tileSize);
        }
        styleParameters.compOp = _builderParameters.compOp;
        styleParameters.glyphRenderSize = _builderParameters.glyphRenderSize;

        if (_builderParameters.strokeMap) {
            bool strokeUsed = std::any_of(_builderParameters.lineStrokeIds.begin(), _builderParameters.lineStrokeIds.begin() + _builderParameters.parameterCount, [](StrokeMap::StrokeId strokeId) { return strokeId != 0; });
            if (strokeUsed) {
                styleParameters.pattern = _builderParameters.strokeMap->getBitmapPattern();
            }
        }
        else if (_builderParameters.glyphMap) {
            styleParameters.pattern = _builderParameters.glyphMap->getBitmapPattern();
        }
        else {
            styleParameters.pattern = _builderParameters.pattern;
        }

        // Transform coordinates, binormals, calculate normals
        VertexArray<cglib::vec3<float>> coords;
        VertexArray<cglib::vec3<float>> normals;
        VertexArray<cglib::vec3<float>> binormals;
        coords.reserve(_coords.size());
        normals.reserve(_coords.size());
        binormals.reserve(_binormals.size());
        for (std::size_t i = 0; i < _coords.size(); i++) {
            coords.append(_transformer->calculatePoint(_coords[i]));
            normals.append(_transformer->calculateNormal(_coords[i]));
            if (!_binormals.empty()) {
                binormals.append(_transformer->calculateVector(_coords[i], _binormals[i]));
            }
        }
        if (std::all_of(normals.begin(), normals.end(), [](const cglib::vec3<float>& normal) { return normal(2) == 1; })) {
            normals.clear();
        }

        // Transform texture coordinates. Note that texture coordinates are also used as local tile coordinates for 3D polygons.
        VertexArray<cglib::vec2<float>> texCoords;
        if (styleParameters.pattern) {
            texCoords.reserve(_texCoords.size());
            for (std::size_t i = 0; i < _texCoords.size(); i++) {
                texCoords.append(cglib::pointwise_product(_texCoords[i], cglib::vec2<float>(1.0f / styleParameters.pattern->bitmap->width, 1.0f / styleParameters.pattern->bitmap->height)));
            }
        }
        else if (_builderParameters.type == TileGeometry::Type::POLYGON3D) {
            texCoords.reserve(_texCoords.size());
            texCoords.copy(_texCoords, 0, _texCoords.size());
        }

        // Transform heights
        VertexArray<float> heights;
        heights.reserve(_heights.size());
        for (std::size_t i = 0; i < _heights.size(); i++) {
            heights.append(_transformer->calculateHeight(_coords[i], _heights[i]));
        }

        // Compress attributes
        VertexArray<cglib::vec4<std::int8_t>> attribs;
        attribs.copy(_attribs, 0, _attribs.size());
        // A variant is repointed by rewriting the style byte, so the attributes have to survive even
        // when both of its slots happen to be slot 0.
        if (_styleVariantRanges.empty() && std::all_of(attribs.begin(), attribs.end(), [](const cglib::vec4<std::int8_t>& attrib) { return attrib == cglib::vec4<std::int8_t>(0, 0, 0, 0); })) {
            attribs.clear();
        }

        // Calculate number of dimensions required for coordinates/binormals
        int dimensions = 2;
        if (std::any_of(coords.begin(), coords.end(), [](const cglib::vec3<float>& coord) { return coord(2) != 0; })) {
            dimensions = 3;
        }
        else if (std::any_of(normals.begin(), normals.end(), [](const cglib::vec3<float>& normal) { return normal(2) != 1; })) {
            dimensions = 3;
        }
        else if (std::any_of(binormals.begin(), binormals.end(), [](const cglib::vec3<float>& binormal) { return binormal(2) != 0; })) {
            dimensions = 3;
        }

        // The variant each vertex belongs to, so the runs can be rebuilt after the repacking below
        // renumbers and drops vertices.
        std::vector<int> vertexVariants;
        if (!_styleVariantRanges.empty()) {
            vertexVariants.assign(_coords.size(), -1);
            for (std::size_t i = 0; i < _styleVariantRanges.size(); i++) {
                const StyleVariantRange& range = _styleVariantRanges[i];
                std::fill(vertexVariants.begin() + range.firstVertex, vertexVariants.begin() + std::min(range.firstVertex + range.vertexCount, vertexVariants.size()), static_cast<int>(i));
            }
        }

        // Split/repack geometry
        float coordScale = calculateScale(coords, _indices);
        float binormalScale = calculateScale(binormals, _indices);
        // For an extrusion the texcoord slot carries the footprint centroid, and the vertex stage
        // hands it straight to applyTerrain - which expects raw coord units. Same scale, then.
        float texCoordScale = (_builderParameters.type == TileGeometry::Type::POLYGON3D ? coordScale : calculateScale(texCoords, _indices));
        float heightScale = calculateScale(heights, _indices);
        for (std::size_t offset = 0; offset < _indices.size(); ) {
            std::size_t count = std::min(std::size_t(65535), _indices.size() - offset);

            std::vector<std::size_t> indexTable(coords.size(), 65536);
            VertexArray<cglib::vec3<float>> remappedCoords;
            remappedCoords.reserve(coords.size());
            VertexArray<cglib::vec4<std::int8_t>> remappedAttribs;
            remappedAttribs.reserve(attribs.size());
            VertexArray<cglib::vec2<float>> remappedTexCoords;
            remappedTexCoords.reserve(texCoords.size());
            VertexArray<cglib::vec3<float>> remappedNormals;
            remappedNormals.reserve(normals.size());
            VertexArray<cglib::vec3<float>> remappedBinormals;
            remappedBinormals.reserve(binormals.size());
            VertexArray<float> remappedHeights;
            remappedHeights.reserve(heights.size());
            VertexArray<std::size_t> remappedIndices;
            remappedIndices.reserve(count);
            VertexArray<long long> remappedIds;
            remappedIds.reserve(count);
            VertexArray<std::uint16_t> remappedGeoPosIndexes;
            remappedGeoPosIndexes.reserve(count);
            std::vector<TileGeometry::FeatureStyleRange> remappedStyleRanges;
            int lastVariant = -1;
            for (std::size_t i = 0; i < count; i++) {
                std::size_t index = _indices[offset + i];
                std::size_t remappedIndex = indexTable[index];
                if (remappedIndex == 65536) {
                    remappedIndex = remappedCoords.size();
                    indexTable[index] = remappedIndex;

                    remappedCoords.append(coords[index]);
                    if (!attribs.empty()) {
                        remappedAttribs.append(attribs[index]);
                    }
                    if (!texCoords.empty()) {
                        remappedTexCoords.append(texCoords[index]);
                    }
                    if (!normals.empty()) {
                        remappedNormals.append(normals[index]);
                    }
                    if (!binormals.empty()) {
                        remappedBinormals.append(binormals[index]);
                    }
                    if (!heights.empty()) {
                        remappedHeights.append(heights[index]);
                    }
                    if (int variant = vertexVariants.empty() ? -1 : vertexVariants[index]; variant >= 0) {
                        // The vertices arrive in first-touch order, so a variant is a run here as
                        // well - just not the same run it was before the repacking.
                        if (variant == lastVariant && remappedStyleRanges.back().firstVertex + remappedStyleRanges.back().vertexCount == remappedIndex) {
                            remappedStyleRanges.back().vertexCount++;
                        }
                        else {
                            const StyleVariantRange& range = _styleVariantRanges[variant];
                            std::uint8_t styleIndex = static_cast<std::uint8_t>(attribs.empty() ? 0 : attribs[index](0));
                            remappedStyleRanges.push_back(TileGeometry::FeatureStyleRange { range.stateKey, static_cast<std::uint32_t>(remappedIndex), 1, styleIndex, { range.styleIndices[0], range.styleIndices[1] } });
                        }
                    }
                    lastVariant = vertexVariants.empty() ? -1 : vertexVariants[index];
                }

                remappedIndices.append(remappedIndex);
                remappedIds.append(_ids[offset + i]);
                remappedGeoPosIndexes.append(_geoPosIndexes[offset + i]);
            }

            packGeometry(_builderParameters.type, dimensions, coordScale, binormalScale, texCoordScale, heightScale, remappedCoords, remappedTexCoords, remappedNormals, remappedBinormals, remappedHeights, remappedAttribs, remappedIndices, remappedIds, remappedGeoPosIndexes, styleParameters, std::move(remappedStyleRanges), geometryList);

            offset += count;
        }
    }

    void TileLayerBuilder::packGeometry(TileGeometry::Type type, int dimensions, float coordScale, float binormalScale, float texCoordScale, float heightScale, const VertexArray<cglib::vec3<float>>& coords, const VertexArray<cglib::vec2<float>>& texCoords, const VertexArray<cglib::vec3<float>>& normals, const VertexArray<cglib::vec3<float>>& binormals, const VertexArray<float>& heights, const VertexArray<cglib::vec4<std::int8_t>>& attribs, const VertexArray<std::size_t>& indices, const VertexArray<long long>& ids, const VertexArray<std::uint16_t>& geoPosIndexes, const TileGeometry::StyleParameters& styleParameters, std::vector<TileGeometry::FeatureStyleRange> featureStyleRanges, std::vector<std::shared_ptr<TileGeometry>>& geometryList) const {
        if (indices.empty()) {
            return;
        }
        
        // Build geometry layout info
        TileGeometry::VertexGeometryLayoutParameters vertexGeomLayoutParams;
        vertexGeomLayoutParams.dimensions = dimensions;
        vertexGeomLayoutParams.coordOffset = vertexGeomLayoutParams.vertexSize;
        vertexGeomLayoutParams.vertexSize += dimensions * sizeof(std::int16_t);
        vertexGeomLayoutParams.vertexSize = (vertexGeomLayoutParams.vertexSize + 3) & ~3;

        if (!attribs.empty()) {
            vertexGeomLayoutParams.attribsOffset = vertexGeomLayoutParams.vertexSize;
            vertexGeomLayoutParams.vertexSize += 4 * sizeof(std::int8_t);
        }

        if (!texCoords.empty()) {
            vertexGeomLayoutParams.texCoordOffset = vertexGeomLayoutParams.vertexSize;
            vertexGeomLayoutParams.vertexSize += 2 * sizeof(std::int16_t);
        }

        if (!normals.empty()) {
            vertexGeomLayoutParams.normalOffset = vertexGeomLayoutParams.vertexSize;
            vertexGeomLayoutParams.vertexSize += dimensions * sizeof(std::int16_t);
            vertexGeomLayoutParams.vertexSize = (vertexGeomLayoutParams.vertexSize + 3) & ~3;
        }

        if (!binormals.empty()) {
            vertexGeomLayoutParams.binormalOffset = vertexGeomLayoutParams.vertexSize;
            vertexGeomLayoutParams.vertexSize += dimensions * sizeof(std::int16_t);
            vertexGeomLayoutParams.vertexSize = (vertexGeomLayoutParams.vertexSize + 3) & ~3;
        }

        if (!heights.empty()) {
            vertexGeomLayoutParams.heightOffset = vertexGeomLayoutParams.vertexSize;
            vertexGeomLayoutParams.vertexSize += sizeof(std::int16_t);
            vertexGeomLayoutParams.vertexSize = (vertexGeomLayoutParams.vertexSize + 3) & ~3;
        }

        vertexGeomLayoutParams.coordScale = coordScale;
        vertexGeomLayoutParams.binormalScale = binormalScale;
        vertexGeomLayoutParams.texCoordScale = texCoordScale;
        vertexGeomLayoutParams.heightScale = heightScale;

        // Interleave, compress actual geometry data
        VertexArray<std::uint8_t> compressedVertexGeometry;
        compressedVertexGeometry.fill(0, coords.size() * vertexGeomLayoutParams.vertexSize);
        for (std::size_t i = 0; i < coords.size(); i++) {
            std::uint8_t* baseCompressedPtr = &compressedVertexGeometry[i * vertexGeomLayoutParams.vertexSize];

            const cglib::vec3<float>& coord = coords[i];
            std::int16_t* compressedCoordPtr = reinterpret_cast<std::int16_t*>(baseCompressedPtr + vertexGeomLayoutParams.coordOffset);
            for (int j = 0; j < dimensions; j++) {
                compressedCoordPtr[j] = static_cast<std::int16_t>(coord(j) * coordScale);
            }

            if (!attribs.empty()) {
                const cglib::vec4<std::int8_t>& attrib = attribs[i];
                std::int8_t* compressedAttribsPtr = reinterpret_cast<std::int8_t*>(baseCompressedPtr + vertexGeomLayoutParams.attribsOffset);
                compressedAttribsPtr[0] = attrib(0);
                compressedAttribsPtr[1] = attrib(1);
                compressedAttribsPtr[2] = attrib(2);
                compressedAttribsPtr[3] = attrib(3);
            }

            if (!texCoords.empty()) {
                const cglib::vec2<float>& texCoord = texCoords[i];
                std::int16_t* compressedTexCoordPtr = reinterpret_cast<std::int16_t*>(baseCompressedPtr + vertexGeomLayoutParams.texCoordOffset);
                compressedTexCoordPtr[0] = static_cast<std::int16_t>(texCoord(0) * texCoordScale);
                compressedTexCoordPtr[1] = static_cast<std::int16_t>(texCoord(1) * texCoordScale);
            }

            if (!normals.empty()) {
                const cglib::vec3<float>& normal = normals[i];
                std::int16_t* compressedNormalPtr = reinterpret_cast<std::int16_t*>(baseCompressedPtr + vertexGeomLayoutParams.normalOffset);
                for (int j = 0; j < dimensions; j++) {
                    compressedNormalPtr[j] = static_cast<std::int16_t>(normal(j) * 32767.0f); // assume strict range -1..1
                }
            }

            if (!binormals.empty()) {
                const cglib::vec3<float>& binormal = binormals[i];
                std::int16_t* compressedBinormalPtr = reinterpret_cast<std::int16_t*>(baseCompressedPtr + vertexGeomLayoutParams.binormalOffset);
                for (int j = 0; j < dimensions; j++) {
                    compressedBinormalPtr[j] = static_cast<std::int16_t>(binormal(j) * binormalScale);
                }
            }

            if (!heights.empty()) {
                float height = heights[i];
                std::int16_t* compressedHeightPtr = reinterpret_cast<std::int16_t*>(baseCompressedPtr + vertexGeomLayoutParams.heightOffset);
                compressedHeightPtr[0] = static_cast<std::int16_t>(height * heightScale);
            }
        }

        // Compress indices
        size_t size = indices.size();
        VertexArray<std::uint16_t> compressedIndices;
        compressedIndices.reserve(indices.size());
        for (std::size_t i = 0; i < size; i++) {
            compressedIndices.append(static_cast<std::uint16_t>(indices[i]));
        }

        // Compress ids
        std::vector<std::pair<std::size_t, long long>> compressedIds;
        if (!ids.empty()) {
            std::size_t offset = 0;
            size_t size = ids.size();
            for (std::size_t i = 1; i < size; i++) {
                if (ids[i] != ids[offset]) {
                    compressedIds.emplace_back(i - offset, ids[offset]);
                    offset = i;
                }
            }
            compressedIds.emplace_back(ids.size() - offset, ids[offset]);
            compressedIds.shrink_to_fit();
        }
        // Compress geoPosIndexes
        std::vector<std::pair<std::size_t, std::uint16_t>> compressedGeoPosIndexes;
        if (!geoPosIndexes.empty()) {
            std::size_t offset = 0;
            size_t size = geoPosIndexes.size();
            for (std::size_t i = 1; i < size; i++) {
                if (geoPosIndexes[i] != geoPosIndexes[offset]) {
                    compressedGeoPosIndexes.emplace_back(i - offset, geoPosIndexes[offset]);
                    offset = i;
                }
            }
            compressedGeoPosIndexes.emplace_back(geoPosIndexes.size() - offset, geoPosIndexes[offset]);
            compressedGeoPosIndexes.shrink_to_fit();
        }

        // Store geometry
        auto geometry = std::make_shared<TileGeometry>(type, _geomScale, styleParameters, vertexGeomLayoutParams, std::move(compressedVertexGeometry), std::move(compressedIndices), std::move(compressedIds), std::move(compressedGeoPosIndexes));
        if (!featureStyleRanges.empty()) {
            geometry->setFeatureStyleRanges(std::move(featureStyleRanges), _styleState, _stateKey);
        }
        geometryList.push_back(std::move(geometry));
    }

    bool TileLayerBuilder::tesselateGlyph(const cglib::vec2<float>& point, std::int8_t styleIndex, const cglib::vec2<float>& pen, const cglib::vec2<float>& size, const GlyphMap::Glyph* glyph) {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        cglib::vec2<float> p0 = pen, p3 = pen + size;
        cglib::vec4<std::int8_t> attrib(styleIndex, 0, 0, 0);
        if (glyph) {
            u0 = static_cast<float>(glyph->x); // NOTE: u,v coordinates will be normalized when the layer is built
            v0 = static_cast<float>(glyph->y);
            u1 = static_cast<float>(glyph->x + glyph->width);
            v1 = static_cast<float>(glyph->y + glyph->height);
            attrib(1) = static_cast<std::int8_t>(glyph->mode);
        }

        if (_clipBox.inside(point)) {
            std::size_t i0 = _coords.size();
            _indices.append(i0 + 0, i0 + 2, i0 + 1);
            _indices.append(i0 + 0, i0 + 3, i0 + 2);

            _coords.append(point, point, point, point);
            _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u1, v0), cglib::vec2<float>(u1, v1), cglib::vec2<float>(u0, v1));
            _binormals.append(cglib::vec2<float>(p0(0), p0(1)), cglib::vec2<float>(p3(0), p0(1)), cglib::vec2<float>(p3(0), p3(1)), cglib::vec2<float>(p0(0), p3(1)));
            _attribs.append(attrib, attrib, attrib, attrib);
        }

        return true;
    }

    bool TileLayerBuilder::tesselatePolygon(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, std::int8_t styleIndex, const PolygonStyle& style) {
        _tesselator.clear();
        if (!_tesselator.tesselate(pointsList)) {
            return false;
        }

        float u0 = 0.0f, v0 = 0.0f;
        float du_dx = 0.0f, dv_dy = 0.0f;
        if (style.pattern) {
            u0 = static_cast<float>(std::fmod((_tileId.x + 0.5) * _tileSize, style.pattern->bitmap->width))  / style.pattern->widthScale;
            v0 = static_cast<float>(std::fmod((_tileId.y + 0.5) * _tileSize, style.pattern->bitmap->height)) / style.pattern->heightScale;
            du_dx = _tileSize / style.pattern->widthScale;
            dv_dy = _tileSize / style.pattern->heightScale;
        }

        std::size_t offset = _coords.size();
        for (std::size_t i = 0; i < _tesselator.getVertices().size(); i++) {
            cglib::vec2<float> p = _tesselator.getVertices()[i];
            cglib::vec2<float> uv(u0 + p(0) * du_dx, v0 + p(1) * dv_dy);

            _coords.append(p);
            _texCoords.append(uv);
        }

        for (std::size_t i = 0; i < _tesselator.getElements().size(); i += 3) {
            int i0 = _tesselator.getElements()[i + 0];
            int i1 = _tesselator.getElements()[i + 1];
            int i2 = _tesselator.getElements()[i + 2];

            cglib::bbox2<float> bounds(_coords[i0 + offset]);
            bounds.add(_coords[i1 + offset]);
            bounds.add(_coords[i2 + offset]);
            if (_polygonClipBox.inside(bounds)) {
                std::array<std::size_t, 3> srcIndices = { { i0 + offset, i2 + offset, i1 + offset } };
                _transformer->tesselateTriangles(srcIndices.data(), srcIndices.size(), _coords, _texCoords, _indices);
            }
        }

        _attribs.fill(cglib::vec4<std::int8_t>(styleIndex, 0, 0, 0), _coords.size() - offset);

        return true;
    }

    float TileLayerBuilder::insetRings(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float radius, std::vector<std::vector<cglib::vec2<float>>>& insetList) const {
        insetList = pointsList;
        bool any = false;
        for (std::size_t ring = 0; ring < pointsList.size(); ring++) {
            const std::vector<cglib::vec2<float>>& points = pointsList[ring];
            if (points.size() < 3) {
                return 0.0f;
            }
            // Which way is IN, from the ring's winding, exactly as the ground skirt does.
            float area = 0.0f;
            for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
                area += points[j](0) * points[i](1) - points[i](0) * points[j](1);
            }
            float sign = area < 0.0f ? -1.0f : 1.0f;

            for (std::size_t i = 0; i < points.size(); i++) {
                std::size_t prev = (i + points.size() - 1) % points.size();
                std::size_t next = (i + 1) % points.size();
                cglib::vec2<float> ePrev = points[i] - points[prev];
                cglib::vec2<float> eNext = points[next] - points[i];
                float lenPrev = cglib::length(ePrev);
                float lenNext = cglib::length(eNext);
                if (!(lenPrev > 0.0f) || !(lenNext > 0.0f)) {
                    return 0.0f; // a ring that touches itself
                }
                cglib::vec2<float> tPrev = ePrev * (1.0f / lenPrev);
                cglib::vec2<float> tNext = eNext * (1.0f / lenNext);
                cglib::vec2<float> nPrev = cglib::vec2<float>(tPrev(1), -tPrev(0)) * sign;
                cglib::vec2<float> nNext = cglib::vec2<float>(tNext(1), -tNext(0)) * sign;

                // The miter is 1/cos(halfAngle) - NOT 2/(1 + dot), which is that value squared and
                // over-insets every corner (2x instead of 1.41x at a right angle, far worse when
                // sharp). Capped at 4, past which the corner is simply cut.
                float cosHalfAngle = std::sqrt(std::max(0.0f, 0.5f * (1.0f + cglib::dot_product(nPrev, nNext))));
                cglib::vec2<float> bisector = nPrev + nNext;
                float bisectorLen = cglib::length(bisector);
                if (!(bisectorLen > 0.0f) || !(cosHalfAngle > 0.0f)) {
                    return 0.0f; // the two edges double back on each other
                }
                bisector = bisector * (1.0f / bisectorLen);
                insetList[ring][i] = points[i] - bisector * (radius * std::min(4.0f, 1.0f / cosHalfAngle));
                any = true;
            }
        }
        return any ? radius : 0.0f;
    }

    void TileLayerBuilder::appendBevelQuad(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1, const cglib::vec2<float>& i0p, const cglib::vec2<float>& i1p, const cglib::vec2<float>& binormal, float wallTop, float roofHeight, std::int8_t styleIndex) {
        auto packT = [this](float h) -> std::int8_t {
            float t = _polygon3DGradientHeight > 0.0f ? h / _polygon3DGradientHeight : 1.0f;
            return static_cast<std::int8_t>(std::max(0, std::min(127, static_cast<int>(std::lround(t * 127.0f)))));
        };
        std::int8_t tLo = packT(wallTop);
        std::int8_t tHi = packT(roofHeight);
        // 127 at the wall's normal, 0 at the roof's: the vertex stage blends the two and that
        // interpolation IS the rounding. Held at 64 across the whole band instead, the bevel
        // becomes a flat facet - one tone belonging to neither wall nor roof, which reads as a rim
        // around every roof and is what makes shapes separable looking straight down.
        std::int8_t sideLo = _polygon3DRoundedRoof ? static_cast<std::int8_t>(127) : static_cast<std::int8_t>(64);
        std::int8_t sideHi = _polygon3DRoundedRoof ? static_cast<std::int8_t>(0) : static_cast<std::int8_t>(64);
        const cglib::vec4<std::int8_t> low0(styleIndex, sideLo, 0, tLo);
        const cglib::vec4<std::int8_t> high0(styleIndex, sideHi, 0, tHi);

        std::size_t i0 = _coords.size();
        _coords.append(p0, p1, i1p, i0p);
        _texCoords.append(_polygon3DCentroid, _polygon3DCentroid, _polygon3DCentroid, _polygon3DCentroid);
        _binormals.append(binormal, binormal, binormal, binormal);
        _heights.append(wallTop, wallTop, roofHeight, roofHeight);
        _attribs.append(low0, low0, high0, high0);
        _indices.append(i0 + 0, i0 + 1, i0 + 2);
        _indices.append(i0 + 0, i0 + 2, i0 + 3);
    }

    // Tile-local length of one contact-shadow quad along a wall. 1/32 of a tile is the regular
    // terrain grid's own cell, which is the resolution the surface actually bends at.
    static constexpr float GROUND_SKIRT_STEP = 1.0f / 64.0f;

    void TileLayerBuilder::appendGroundSkirt(const std::vector<cglib::vec2<float>>& points, float height, bool hole, std::int8_t styleIndex) {
        if (points.size() < 3 || _polygon3DGroundRadius <= 0.0f) {
            return;
        }
        // Only footprints that reach this TILE. The walls have always been clipped this way and the
        // shadow never was, so under overzoom - where one source tile's features are handed to every
        // target tile derived from it - each tile laid the shadow of every building in the source.
        cglib::bbox2<float> bounds;
        for (const cglib::vec2<float>& p : points) {
            bounds.add(p);
        }
        if (!_polygonClipBox.inside(bounds)) {
            return;
        }
        // Metres to tile-local, as the walls convert their heights. mapbox divides the AO ground
        // radius by 3.5 before using it, so the style's metres mean there what they mean here.
        float radius = _transformer->calculateHeight(points[0], _polygon3DGroundRadius / 3.5f);
        if (!(radius > 0.0f)) {
            return;
        }
        float invRadius = 1.0f / radius;

        // Which side of an edge the building stands on, so the fragment can hold the band at full
        // strength there instead of letting it fall off under the walls. The left normal below
        // points into a counter-clockwise ring - and OUT of a hole ring, where the material is the
        // side the ring does not enclose. Which ring is a hole cannot be read from its winding
        // here: the tile data does not guarantee holes wind the other way, and a courtyard whose
        // winding matched its outer ring came out filled solid.
        float area2 = 0.0f;
        for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            area2 += points[j](0) * points[i](1) - points[i](0) * points[j](1);
        }
        float inwardSign = (area2 > 0.0f ? 1.0f : -1.0f) * (hole ? -1.0f : 1.0f);

        // One quad per edge, covering that edge's bounding CAPSULE: the fragment measures its own
        // distance to the segment, so the caps round every corner and join one edge's shadow to the
        // next without any outline, offset or union here. Overlaps - between edges, between a
        // building and its parts, between neighbours - are resolved by MIN blending in the mask
        // pass, which is what stops them compounding towards black.
        for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            cglib::vec2<float> delta = points[i] - points[j];
            float len = cglib::length(delta);
            if (!(len > 0.0f)) {
                continue;
            }
            cglib::vec2<float> tangent = delta * (1.0f / len);
            cglib::vec2<float> normal(-tangent(1), tangent(0));
            cglib::vec2<float> offset = normal * radius;
            float segLen = len * invRadius;

            // Split ALONG the wall, at the terrain lattice's own step. The quad's four corners land
            // on the surface but its interior interpolates linearly between them, so one quad over
            // a 50 m wall cuts into a slope at one end and floats at the other. Across the wall the
            // span is only 2 * radius, so that direction needs no split.
            float span = len + 2.0f * radius;
            float step = (_polygon3DGroundStep > 0.0f ? _transformer->calculateHeight(points[0], _polygon3DGroundStep) : GROUND_SKIRT_STEP);
            int steps = std::max(1, std::min(128, static_cast<int>(std::ceil(span / std::max(1.0e-6f, step)))));
            cglib::vec4<std::int8_t> attribs(styleIndex, 0, 0, 0);
            for (int k = 0; k < steps; k++) {
                // Distance along the segment's own frame, from -radius (the near cap) onward.
                float tA = -radius + span * (static_cast<float>(k) / steps);
                float tB = -radius + span * (static_cast<float>(k + 1) / steps);
                cglib::vec2<float> a = points[j] + tangent * tA;
                cglib::vec2<float> b = points[j] + tangent * tB;
                float alongA = tA * invRadius;
                float alongB = tB * invRadius;

                std::size_t i0 = _groundCoords.size();
                _groundCoords.append(a - offset, b - offset, b + offset, a + offset);
                _groundTexCoords.append(a - offset, b - offset, b + offset, a + offset);
                // Affine in the vertex, so interpolating it over the quad is exact.
                _groundBinormals.append(cglib::vec3<float>(alongA, -inwardSign, segLen), cglib::vec3<float>(alongB, -inwardSign, segLen),
                                        cglib::vec3<float>(alongB, inwardSign, segLen), cglib::vec3<float>(alongA, inwardSign, segLen));
                _groundHeights.append(height, height, height, height);
                _groundAttribs.append(attribs, attribs, attribs, attribs);
                _groundIndices.append(i0 + 0, i0 + 1, i0 + 2);
                _groundIndices.append(i0 + 0, i0 + 2, i0 + 3);
            }
        }
    }

    bool TileLayerBuilder::appendRoof(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float baseHeight, float roofHeight, RoofShape shape, std::int8_t styleIndex) {
        if (shape == RoofShape::FLAT || !(roofHeight > 0.0f) || pointsList.empty() || pointsList[0].size() < 3) {
            return false;
        }
        const std::vector<cglib::vec2<float>>& points = pointsList[0];
        // Only the outer ring carries a shaped roof. A footprint with a courtyard has no single
        // apex or ridge, and OSM tags one anyway - flat is the honest answer there.
        if (pointsList.size() > 1) {
            return false;
        }

        // Area centroid, not the average of the vertices: a footprint digitised with more points
        // along one side would drag a vertex average towards it and lean the apex.
        float area = 0.0f;
        cglib::vec2<float> centroid(0, 0);
        for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            float cross = points[j](0) * points[i](1) - points[i](0) * points[j](1);
            area += cross;
            centroid = centroid + (points[j] + points[i]) * cross;
        }
        if (!(std::abs(area) > 0.0f)) {
            return false;
        }
        centroid = centroid * (1.0f / (3.0f * area));

        // The apex line. A pyramid collapses it to the centroid; a gable stretches it along the
        // footprint's longest axis, which is the ridge an OSM 'gabled' roof means without carrying
        // a direction. Taken from the longest EDGE rather than a full oriented bounding box: a
        // building long enough to read as gabled has its ridge parallel to its longest wall.
        cglib::vec2<float> ridge(0, 0);
        if (shape == RoofShape::GABLED) {
            float longest = 0.0f;
            for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
                cglib::vec2<float> edge = points[i] - points[j];
                float len = cglib::length(edge);
                if (len > longest) {
                    longest = len;
                    ridge = edge * (1.0f / len);
                }
            }
            // Half the footprint's extent along the ridge, so the two ends sit inside it.
            float minT = 0.0f, maxT = 0.0f;
            for (std::size_t i = 0; i < points.size(); i++) {
                float t = cglib::dot_product(points[i] - centroid, ridge);
                minT = std::min(minT, t);
                maxT = std::max(maxT, t);
            }
            ridge = ridge * (0.5f * (maxT - minT) * 0.5f);
        }

        float apexHeight = baseHeight + roofHeight;
        for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            if (points[i] == points[j]) {
                continue;
            }
            // Which end of the ridge this edge faces. For a pyramid both are the centroid, so the
            // fan degenerates to the apex and the two triangles below become one.
            cglib::vec2<float> apex0 = centroid + ridge * (cglib::dot_product(points[j] - centroid, ridge) < 0.0f ? -1.0f : 1.0f);
            cglib::vec2<float> apex1 = centroid + ridge * (cglib::dot_product(points[i] - centroid, ridge) < 0.0f ? -1.0f : 1.0f);

            cglib::vec2<float> tangent(cglib::unit(points[i] - points[j]));
            cglib::vec2<float> binormal = cglib::vec2<float>(tangent(1), -tangent(0));

            std::size_t i0 = _coords.size();
            _coords.append(points[j], points[i], apex1, apex0);
            _texCoords.append(_polygon3DCentroid, _polygon3DCentroid, _polygon3DCentroid, _polygon3DCentroid);
            _binormals.append(binormal, binormal, binormal, binormal);
            _heights.append(baseHeight, baseHeight, apexHeight, apexHeight);
            // A roof slope is neither wall nor flat top: 64 leans its normal half way between the
            // two, which is what a pitched surface catches of the sun.
            const cglib::vec4<std::int8_t> eave(styleIndex, 64, 0, 127);
            const cglib::vec4<std::int8_t> peak(styleIndex, 0, 0, 127);
            _attribs.append(eave, eave, peak, peak);
            _indices.append(i0 + 0, i0 + 1, i0 + 2);
            if (apex0 != apex1) {
                _indices.append(i0 + 0, i0 + 2, i0 + 3);
            }
        }
        return true;
    }

    std::uint64_t TileLayerBuilder::roofKey(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float height) {
        // Summed, so the ring's start vertex and its winding do not matter - the same building
        // arriving from two source layers is rarely digitised identically.
        std::uint64_t sum = 0;
        std::uint64_t count = 0;
        for (const std::vector<cglib::vec2<float>>& points : pointsList) {
            for (const cglib::vec2<float>& p : points) {
                sum += (static_cast<std::uint64_t>(static_cast<int>(std::lround(p(0) * 32768.0f)) + 128) << 20)
                     ^ static_cast<std::uint64_t>(static_cast<int>(std::lround(p(1) * 32768.0f)) + 128);
                count++;
            }
        }
        return (sum * 1099511628211ULL) ^ (count << 40) ^ static_cast<std::uint64_t>(std::lround(height * 1024.0f));
    }

    std::uint64_t TileLayerBuilder::wallEdgeKey(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1) const {
        // 1/32768 of a tile - 7 cm at zoom 14, finer than any tiler's grid, so two features that
        // share a wall in the source share a key here. The clip box reaches slightly outside the
        // tile, hence the offset that keeps the quantised value unsigned.
        auto quantize = [](float v) -> std::uint64_t {
            return static_cast<std::uint64_t>(std::max(0, std::min(65535, static_cast<int>(std::lround(v * 32768.0f)) + 128)));
        };
        std::uint64_t a = (quantize(p0(0)) << 16) | quantize(p0(1));
        std::uint64_t b = (quantize(p1(0)) << 16) | quantize(p1(1));
        // Undirected: the two features walk their shared edge in opposite directions.
        std::uint64_t edge = (a < b ? (a << 32) | b : (b << 32) | a);
        // ...and the ANCHOR that wall stands on. Two features sharing an edge only have coincident
        // walls if they are lifted to the same height, and that is now per footprint (see
        // tesselatePolygon3D). Suppressing the second wall regardless left a hole in the building
        // wherever the two anchors differ - one wall missing where two wings meet.
        std::uint64_t anchor = (quantize(_polygon3DCentroid(0)) << 16) | quantize(_polygon3DCentroid(1));
        anchor = anchor * 2654435761ULL + static_cast<std::uint64_t>(static_cast<std::uint8_t>(_polygon3DExtent));
        return edge ^ (anchor + 0x9e3779b97f4a7c15ULL + (edge << 6) + (edge >> 2));
    }

    void TileLayerBuilder::appendWallQuad(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1, const cglib::vec2<float>& binormal, float lo, float hi, std::int8_t styleIndex) {
        // The facade gradient, evaluated HERE rather than in the shader: this height and the reach
        // are both style values in the same units, while the shader's height carries a packing and
        // a tile scale (see uAbsHeightScale) that no constant in metres can be compared against.
        // Absolute, so every part of a building shares one ramp instead of restarting per wall.
        auto packT = [this](float h) -> std::int8_t {
            float t = _polygon3DGradientHeight > 0.0f ? h / _polygon3DGradientHeight : 1.0f;
            return static_cast<std::int8_t>(std::max(0, std::min(127, static_cast<int>(std::lround(t * 127.0f)))));
        };
        std::int8_t tLo = packT(lo);
        std::int8_t tHi = packT(hi);

        std::size_t i0 = _coords.size();
        _coords.append(p0, p1, p1);
        _texCoords.append(_polygon3DCentroid, _polygon3DCentroid, _polygon3DCentroid);
        _binormals.append(binormal, binormal, binormal);
        _heights.append(lo, lo, hi);
        _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 127, 0, tLo), cglib::vec4<std::int8_t>(styleIndex, 127, 0, tLo), cglib::vec4<std::int8_t>(styleIndex, 127, 1, tHi));
        _indices.append(i0 + 0, i0 + 1, i0 + 2);

        std::size_t i1 = _coords.size();
        _coords.append(p1, p0, p0);
        _texCoords.append(_polygon3DCentroid, _polygon3DCentroid, _polygon3DCentroid);
        _binormals.append(binormal, binormal, binormal);
        _heights.append(hi, hi, lo);
        _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 127, 1, tHi), cglib::vec4<std::int8_t>(styleIndex, 127, 1, tHi), cglib::vec4<std::int8_t>(styleIndex, 127, 0, tLo));
        _indices.append(i1 + 0, i1 + 1, i1 + 2);
    }

    bool TileLayerBuilder::tesselatePolygon3D(const std::vector<std::vector<cglib::vec2<float>>>& rawPointsList, float minHeight, float maxHeight, std::int8_t styleIndex, const Polygon3DStyle& style) {
        _tesselator.clear();
        // Drop repeated points, including the one an MVT ring closes with. A zero-length edge only
        // ever produced a zero-area wall quad, but the bevel spans corner to corner and turns one
        // into a NaN tangent - a vertex that quantises to garbage and streaks across the tile.
        std::vector<std::vector<cglib::vec2<float>>> pointsList;
        pointsList.reserve(rawPointsList.size());
        for (const std::vector<cglib::vec2<float>>& raw : rawPointsList) {
            std::vector<cglib::vec2<float>> points;
            points.reserve(raw.size());
            for (const cglib::vec2<float>& p : raw) {
                if (points.empty() || points.back() != p) {
                    points.push_back(p);
                }
            }
            while (points.size() > 1 && points.back() == points.front()) {
                points.pop_back();
            }
            pointsList.push_back(std::move(points));
        }
        // The anchor every vertex of this extrusion is elevated at: the mean of the OUTER ring, as
        // maplibre's fill-extrusion does. A building is a rigid prism standing at one elevation -
        // sampling the terrain per vertex instead shears the roof down the slope.
        _polygon3DCentroid = cglib::vec2<float>(0, 0);
        if (!pointsList.empty() && !pointsList[0].empty()) {
            cglib::vec2<float> centroid(0, 0);
            for (const cglib::vec2<float>& p : pointsList[0]) {
                centroid = centroid + p;
            }
            centroid = centroid * (1.0f / pointsList[0].size());
            // Through the transformer, like the coords beside it: calculatePoint FLIPS Y, and the
            // vertex stage hands this straight to applyTerrain. Stored unflipped it samples the
            // elevation at a mirrored position - which on a hillside is a different hill.
            cglib::vec3<float> anchor = _transformer->calculatePoint(centroid);
            _polygon3DCentroid = cglib::vec2<float>(anchor(0), anchor(1));
            // ...and how far the footprint reaches from it, so the vertex stage can find the
            // HIGHEST ground the building stands on and lift the whole prism above it. Anchored at
            // the centroid alone, everything uphill of it is swallowed by the slope.
            // In 1/512 of a tile, which is 1 m at zoom 16 and caps the reach at a quarter tile.
            float extent = 0.0f;
            for (const cglib::vec2<float>& p : pointsList[0]) {
                extent = std::max(extent, cglib::length(p - centroid));
            }
            _polygon3DExtent = static_cast<std::int8_t>(std::min(127.0f, std::round(extent * 512.0f)));
        }
        // Edge radius: the wall stops short of the roof and a bevel band bridges the two, with the
        // roof ring inset by the same amount. What makes it read as ROUNDED is that the band's
        // normals interpolate from the wall's to the roof's - one quad per edge, not a fillet.
        // Skipped for a building too short to give up the height, or one whose footprint has an
        // edge too short to inset without folding the ring through itself.
        float edgeRadius = 0.0f;
        float wallTop = maxHeight;
        std::vector<std::vector<cglib::vec2<float>>> roofList = pointsList;
        // A shaped roof puts its eaves on the ORIGINAL footprint, so an inset top ring would leave
        // the two disagreeing along every edge. The roof is the cap in that case; the bevel is not.
        bool shapedRoof = style.roofShape != RoofShape::FLAT && style.roofHeight > 0.0f;
        if (!shapedRoof && _polygon3DEdgeRadius > 0.0f && maxHeight - minHeight > 2.0f * _polygon3DEdgeRadius && !pointsList.empty() && !pointsList[0].empty()) {
            // The horizontal inset is TILE-LOCAL and the vertical drop is in the style's metres, so
            // the clamp the ring imposes has to come back through the same conversion or the bevel
            // is taller than it is wide.
            float localPerMeter = _transformer->calculateHeight(pointsList[0][0], 1.0f);
            float inset = insetRings(pointsList, _polygon3DEdgeRadius * localPerMeter, roofList);
            if (inset > 0.0f && localPerMeter > 0.0f) {
                edgeRadius = inset / localPerMeter;
                wallTop = maxHeight - edgeRadius;
            }
        }

        if (!_tesselator.tesselate(roofList)) {
            return false;
        }

        // A duplicated footprint puts two roofs on one plane, which z-fights exactly as the walls
        // do. Decided BEFORE the walls, because the bevel belongs to the roof: gated on the wall
        // instead, a feature whose wall was deduped away still drew its inset roof and left an
        // unclosed ring around it - a slit right where the bevel should be.
        bool drawRoof = _polygon3DRoofs.insert(roofKey(pointsList, maxHeight)).second;

        if (minHeight != maxHeight) {
            for (std::size_t ring = 0; ring < pointsList.size(); ring++) {
                const std::vector<cglib::vec2<float>>& points = pointsList[ring];
                const std::vector<cglib::vec2<float>>& inset = roofList[ring];
                std::size_t j = points.size() - 1;
                for (std::size_t i = 0; i < points.size(); i++) {
                    cglib::bbox2<float> bounds(points[i]);
                    bounds.add(points[j]);
                    if (_polygonClipBox.inside(bounds)) {
                        cglib::vec2<float> tangent(cglib::unit(points[i] - points[j]));
                        cglib::vec2<float> binormal = cglib::vec2<float>(tangent(1), -tangent(0));

                        // An extra ring where the gradient knees, or the per-vertex lighting draws a
                        // straight line from the wall's foot to its roof and the gradient's reach
                        // means nothing (see setPolygon3DGradientHeight).
                        auto emitBand = [this](float lo, float hi, const std::function<void(float, float)>& append) {
                            if (hi <= lo) {
                                return;
                            }
                            float knee = _polygon3DGradientHeight;
                            if (knee > lo && knee < hi) {
                                append(lo, knee);
                                lo = knee;
                            }
                            append(lo, hi);
                        };
                        // Only the part of this wall no other feature has already walled. A
                        // building and its building:part share footprint edges, and the two
                        // coincident walls z-fight into a stipple that reads as shadow acne.
                        auto emit = [&](float lo, float hi) {
                            emitBand(lo, hi, [&](float l, float h) {
                                appendWallQuad(points[i], points[j], binormal, l, h, styleIndex);
                            });
                        };
                        auto it = _polygon3DWalls.find(wallEdgeKey(points[i], points[j]));
                        if (it == _polygon3DWalls.end()) {
                            _polygon3DWalls.emplace(wallEdgeKey(points[i], points[j]), std::make_pair(minHeight, maxHeight));
                            emit(minHeight, wallTop);
                        } else {
                            // Up to two quads: a wall taller at both ends than what is there
                            // sticks out below AND above it.
                            emit(minHeight, std::min(wallTop, it->second.first));
                            emit(std::max(minHeight, it->second.second), wallTop);
                            it->second.first = std::min(it->second.first, minHeight);
                            it->second.second = std::max(it->second.second, maxHeight);
                        }
                        // The bevel closes this feature's own roof, so it follows the ROOF's
                        // dedupe, not the wall's (see drawRoof).
                        //
                        // Mitred: the band spans the TRUE wall top, and at a corner the two
                        // adjacent bands share the edge points[i] -> inset[i] exactly, so nothing
                        // has to fill the corner. The wall keeps its real footprint all the way
                        // down - cutting it back to make room for a chamfer put the bevel on the
                        // building's BASE too, which is a notch at every ground-level corner.
                        if (edgeRadius > 0.0f && drawRoof) {
                            appendBevelQuad(points[i], points[j], inset[i], inset[j], binormal, wallTop, maxHeight, styleIndex);
                        }
                    }

                    j = i;
                }
            }
        }

        if (!drawRoof) {
            return true;
        }

        // The contact shadow, on the GROUND - not at minHeight. A building:part starting at 20 m
        // would otherwise cast its shadow 20 m up, floating beside the one its parent casts at 0.
        // Only for a footprint that is actually EXTRUDED and STANDS ON THE GROUND: a flat one was
        // casting a full ring onto open ground with nothing above it, and a building:part starting
        // at 20 m - a bridge deck, a tunnel roof - does not touch the ground it was shadowing.
        if (minHeight <= 0.0f && maxHeight > minHeight) {
            for (std::size_t ring = 0; ring < pointsList.size(); ring++) {
                appendGroundSkirt(pointsList[ring], 0.0f, ring > 0, styleIndex);
            }
        }

        // A shaped roof replaces the flat cap entirely - its slopes already close the top. The
        // bevel above stops at the wall, so an edge radius and a pitched roof do not fight.
        if (appendRoof(pointsList, maxHeight, style.roofHeight, style.roofShape, styleIndex)) {
            return true;
        }

        std::size_t offset = _coords.size();
        for (std::size_t i = 0; i < _tesselator.getVertices().size(); i++) {
            cglib::vec2<float> p = _tesselator.getVertices()[i];

            _coords.append(p);
            _texCoords.append(_polygon3DCentroid);
        }

        for (std::size_t i = 0; i < _tesselator.getElements().size(); i += 3) {
            int i0 = _tesselator.getElements()[i + 0];
            int i1 = _tesselator.getElements()[i + 1];
            int i2 = _tesselator.getElements()[i + 2];

            cglib::bbox2<float> bounds(_coords[i0 + offset]);
            bounds.add(_coords[i1 + offset]);
            bounds.add(_coords[i2 + offset]);
            if (_polygonClipBox.inside(bounds)) {
                // NOT through the transformer's subdivision, unlike a draped 2D polygon: the
                // walls below are already emitted as one quad per footprint edge, so subdividing
                // the roof only buys a roof that follows the terrain more closely than the walls
                // that hold it up - which is not what a roof does. Tangram tesselates its
                // extrusions the same way (Builders::buildPolygon, no refinement), and the
                // triangles saved are pure vertex work: measured on an Adreno 610, a 4x coarser
                // subdivision threshold was already worth 0.5 ms of the extrusion pass.
                _indices.append(i0 + offset, i2 + offset, i1 + offset);
            }
        }

        _binormals.fill(cglib::vec2<float>(0, 0), _coords.size() - offset);
        _heights.fill(maxHeight, _coords.size() - offset);
        _attribs.fill(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 127), _coords.size() - offset); // roof: never darkened

        return true;
    }

    bool TileLayerBuilder::tesselateLine(const std::vector<cglib::vec2<float>>& linePoints, std::int8_t styleIndex, const StrokeMap::Stroke* stroke, const LineStyle& style) {
        if (linePoints.size() < 2) {
            return false;
        }

        float v0 = 0, v1 = 0, du_dl = 0;
        if (stroke) {
            v1 = stroke->y0 + 0.5f;
            v0 = stroke->y1 - 0.5f;
            du_dl = _tileSize / stroke->scale;
        }

        VertexArray<cglib::vec2<float>> points;
        points.reserve(linePoints.size());
        _transformer->tesselateLineString(linePoints.data(), linePoints.size(), points);

        bool cycle = points[0] == points[points.size() - 1];

        // 'arrow only' emits the head and nothing else, so a style can paint it OVER the shaft:
        // the shaft rules draw first, the head rules after, and where the head overlaps its own
        // line - a U-turn, a hairpin - the head keeps the outline that tells it apart from the
        // line it sits on. Drawn from the last segment that has a direction; the head hangs on the
        // last vertex itself, with no pull-back, because there is no line here to pull back.
        if (style.endArrowOnly) {
            if (cycle || !style.hasEndArrow()) {
                return false;
            }
            for (std::size_t k = points.size() - 1; k > 0; k--) {
                if (points[k] == points[k - 1]) {
                    continue;
                }
                cglib::vec2<float> arrowTangent = cglib::unit(points[k] - points[k - 1]);
                cglib::vec2<float> arrowBinormal(arrowTangent(1), -arrowTangent(0));
                return tesselateLineEndArrow(points[k], 0, v0, v1, arrowTangent, arrowBinormal, styleIndex, style);
            }
            return false;
        }

        bool endpoints = !cycle && style.capMode != LineCapMode::NONE;
        // An offset line carries its offset in the vertex shader as binormal * offset * side, so a
        // vertex with a zero binormal is not offset at all: the sharp-join fix below can not be
        // applied to it and such a line keeps the plain (overlapping) split.
        bool offsetLine = !(style.offsetFunc == FloatFunction(0));
        float linePos = 0;

        std::size_t i = 1;
        std::size_t j = 0;
        if (cycle) {
            i = 0;
            j = points.size() - 2; // last point is same as first
            while (j > 0) {
                if (points[i] != points[j]) {
                    break;
                }
                j--;
            }
        }
        while (i < points.size()) {
            if (points[i] != points[j]) {
                break;
            }
            i++;
        }
        if (i >= points.size()) {
            return false;
        }

        cglib::vec2<float> binormal(0, 0), tangent(0, 0);
        {
            const cglib::vec2<float>& p0 = points[j];
            const cglib::vec2<float>& p1 = points[i];
            float u0 = linePos * du_dl;
            cglib::vec2<float> dp(p1 - p0);
            linePos += cglib::length(dp);

            tangent = cglib::unit(dp);
            binormal = cglib::vec2<float>(tangent(1), -tangent(0));

            if (endpoints) {
                std::size_t i0 = _coords.size();
                tesselateLineEndPoint(p0, u0, v0, v1, i0 + 2, i0, -tangent, binormal, styleIndex, style); // refer to the point that will be added after end point
            }

            _coords.append(p0, p0);
            _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
            _binormals.append(-binormal, binormal);
            _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 0), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 0));
        }

        while (++i < points.size()) {
            const cglib::vec2<float>& p0 = points[i - 1];
            const cglib::vec2<float>& p1 = points[i];
            if (p0 == p1) {
                continue;
            }
            float u0 = linePos * du_dl;
            cglib::vec2<float> dp(p1 - p0);
            linePos += cglib::length(dp);

            cglib::vec2<float> prevBinormal = binormal;
            cglib::vec2<float> prevTangent = tangent;
            tangent = cglib::unit(dp);
            binormal = cglib::vec2<float>(tangent(1), -tangent(0));

            std::size_t i0 = _coords.size();

            cglib::bbox2<float> bounds(p0);
            bounds.add(_coords[i0 - 2]);
            if (_clipBox.inside(bounds)) {
                _indices.append(i0 - 1, i0 - 2, i0 + 0);
                _indices.append(i0 - 1, i0 + 0, i0 + 1);
            }

            float dot = cglib::dot_product(binormal, prevBinormal);
            if (dot < style.splitDotLimit && (offsetLine || dot < 0.0f)) {
                if (offsetLine) {
                    // Split line segments
                    _coords.append(p0, p0);
                    _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
                    _binormals.append(-prevBinormal, prevBinormal);
                    _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 0), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 0));

                    _coords.append(p0, p0);
                    _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
                    _binormals.append(-binormal, binormal);
                    _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 0), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 0));
                }
                else {
                    // Split line segments, INNER corners collapsed onto the centre line. Two full
                    // width quads meeting at p0 overlap in a lens on the inside of the turn, and
                    // every pixel of that lens is blended twice - which is what makes a line with
                    // line-opacity go dark at each sharp turn, and a hairpin the worst case of all.
                    // Collapsing both inner corners to p0 (mapbox's inner join) leaves the quads
                    // touching instead of overlapping; one triangle closes the outer gap the plain
                    // split used to leave. The notch this leaves on the inside is only ever cut
                    // where the line reverses onto itself, which is where the line covers it.
                    bool innerSecond = cglib::dot_product(prevTangent, binormal) < 0;
                    cglib::vec2<float> centre(0, 0);
                    cglib::vec2<float> outerTexCoord(u0, innerSecond ? v0 : v1);
                    cglib::vec2<float> centreTexCoord(u0, (v0 + v1) * 0.5f);
                    std::int8_t outerSide = innerSecond ? 1 : -1;
                    cglib::vec4<std::int8_t> outerAttribs(styleIndex, 0, outerSide, 0);
                    cglib::vec4<std::int8_t> centreAttribs(styleIndex, 0, 0, 0);

                    for (int n = 0; n < 2; n++) {
                        const cglib::vec2<float>& b = (n == 0 ? prevBinormal : binormal);
                        _coords.append(p0, p0);
                        if (innerSecond) {
                            _texCoords.append(outerTexCoord, centreTexCoord);
                            _binormals.append(-b, centre);
                            _attribs.append(outerAttribs, centreAttribs);
                        } else {
                            _texCoords.append(centreTexCoord, outerTexCoord);
                            _binormals.append(centre, b);
                            _attribs.append(centreAttribs, outerAttribs);
                        }
                    }

                    // Winding mirrors with the turn side - back faces are culled for 2D geometry.
                    // A round join must stay round HERE too: this branch takes over from the bevel
                    // branch below at 90 degrees, and closing a hairpin with the single triangle
                    // cuts its outer corner flat - the join reads as square, which is what a
                    // simplified route shows at the zooms where simplification leaves a turn
                    // sharper than a right angle.
                    std::size_t hubIndex = i0 + (innerSecond ? 1 : 0);
                    std::size_t lastRimIndex = i0 + (innerSecond ? 0 : 1);
                    std::size_t outerIndexB = i0 + (innerSecond ? 2 : 3);
                    std::size_t fanTriangles = (style.joinMode == LineJoinMode::ROUND ? ROUND_JOIN_TRIANGLES : 1);
                    if (fanTriangles > 1) {
                        cglib::vec2<float> outerA = (innerSecond ? -prevBinormal : prevBinormal);
                        cglib::vec2<float> outerB = (innerSecond ? -binormal : binormal);
                        float cross = outerA(0) * outerB(1) - outerA(1) * outerB(0);
                        float turn = std::atan2(cross, cglib::dot_product(outerA, outerB));
                        if (std::abs(cross) < 1.0e-4f) {
                            // A line reversing exactly onto itself: the two outer offsets are
                            // antiparallel and the cross product no longer carries the direction.
                            // Sweep the half circle that faces AWAY from the incoming segment.
                            cglib::vec2<float> ccwMid(-outerA(1), outerA(0));
                            float halfTurn = boost::math::constants::pi<float>();
                            turn = (cglib::dot_product(ccwMid, prevTangent) < 0 ? halfTurn : -halfTurn);
                        }
                        float step = turn / fanTriangles;
                        float cosStep = std::cos(step), sinStep = std::sin(step);
                        cglib::vec2<float> radial = outerA;
                        for (std::size_t n = 1; n < fanTriangles; n++) {
                            radial = cglib::vec2<float>(radial(0) * cosStep - radial(1) * sinStep, radial(0) * sinStep + radial(1) * cosStep);
                            std::size_t radialIndex = _coords.size();
                            _coords.append(p0);
                            _texCoords.append(outerTexCoord);
                            _binormals.append(radial);
                            _attribs.append(outerAttribs);
                            _indices.append(hubIndex, lastRimIndex, radialIndex);
                            lastRimIndex = radialIndex;
                        }
                    }
                    _indices.append(hubIndex, lastRimIndex, outerIndexB);
                }
            }
            else if ((style.joinMode == LineJoinMode::ROUND && dot < ROUND_JOIN_DOT_LIMIT) || dot < style.miterDotLimit) {
                // Use bevel line join - and, for a round join, a fan of triangles across the outer
                // corner instead of the single flattening triangle. A round join is round at EVERY
                // angle, so it does not wait for the miter limit the way a bevel does.
                cglib::vec2<float> lerpedBinormal = cglib::unit(binormal + prevBinormal);
                std::int8_t sin = static_cast<std::int8_t>(127.0f * cglib::dot_product(prevTangent, lerpedBinormal));
                cglib::vec2<float> lerpedScaledBinormal = lerpedBinormal * (1 / std::sqrt((1 + dot) * 0.5f));
                bool innerSecond = cglib::dot_product(prevTangent, binormal) < 0;

                // The cross-section that ENDS the incoming quad. The next loop iteration links the
                // outgoing quad to the LAST TWO vertices written here, so whatever a round join adds
                // has to sit BETWEEN the two pairs, never after them.
                _coords.append(p0, p0);
                _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
                _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, -sin), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 0));
                if (innerSecond) {
                    _binormals.append(-prevBinormal, lerpedScaledBinormal);
                } else {
                    _binormals.append(-lerpedScaledBinormal, prevBinormal);
                }

                // Round join: a fan across the outer corner. Tangram's addFan, same triangle count,
                // but hubbed on the CENTRE LINE rather than on the miter point: the miter point sits
                // at a full half-width, where the antialias ramp is already down to zero alpha, so a
                // hub there drew five triangles meeting at a transparent vertex - a hairline gap at
                // every join. The inner half of the corner needs no fan anyway, both quads reach the
                // miter point across it.
                std::size_t innerIndex = i0 + (innerSecond ? 1 : 0);
                std::size_t outerIndexA = i0 + (innerSecond ? 0 : 1);
                std::size_t fanTriangles = (style.joinMode == LineJoinMode::ROUND && dot < ROUND_JOIN_DOT_LIMIT ? ROUND_JOIN_TRIANGLES : 1);
                std::size_t lastRimIndex = outerIndexA;
                std::size_t miterIndex = innerIndex;
                if (fanTriangles > 1) {
                    innerIndex = _coords.size();
                    _coords.append(p0);
                    _texCoords.append(cglib::vec2<float>(u0, (v0 + v1) * 0.5f));
                    _binormals.append(cglib::vec2<float>(0, 0));
                    _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 0, 0));
                    // A quad ends on the CHORD from its outer corner to the miter point, which does
                    // not pass through the centre, so a fan hubbed on the centre leaves a sliver
                    // against that chord - a hairline seam at every join. Close it on both sides.
                    if (innerSecond) {
                        _indices.append(innerIndex, miterIndex, outerIndexA);
                    } else {
                        _indices.append(miterIndex, innerIndex, outerIndexA);
                    }
                }
                if (fanTriangles > 1) {
                    cglib::vec2<float> outerA = (innerSecond ? -prevBinormal : prevBinormal);
                    cglib::vec2<float> outerB = (innerSecond ? -binormal : binormal);
                    float turn = std::atan2(outerA(0) * outerB(1) - outerA(1) * outerB(0), cglib::dot_product(outerA, outerB));
                    float step = turn / fanTriangles;
                    float cosStep = std::cos(step), sinStep = std::sin(step);

                    cglib::vec2<float> radial = outerA;
                    for (std::size_t n = 1; n < fanTriangles; n++) {
                        radial = cglib::vec2<float>(radial(0) * cosStep - radial(1) * sinStep, radial(0) * sinStep + radial(1) * cosStep);
                        std::size_t radialIndex = _coords.size();
                        _coords.append(p0);
                        _texCoords.append(cglib::vec2<float>(u0, innerSecond ? v0 : v1));
                        _binormals.append(radial);
                        _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, innerSecond ? 1 : -1, 0));
                        // Winding follows the turn side, as the bevel triangle below does: back
                        // faces are culled for 2D geometry, so a fan wound the same way for both
                        // turn directions loses every join that turns the other way.
                        if (innerSecond) {
                            _indices.append(innerIndex, lastRimIndex, radialIndex);
                        } else {
                            _indices.append(lastRimIndex, innerIndex, radialIndex);
                        }
                        lastRimIndex = radialIndex;
                    }
                }

                // The cross-section that STARTS the outgoing quad - last, so the next iteration
                // finds it where it expects.
                std::size_t i1 = _coords.size();
                _coords.append(p0, p0);
                _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
                _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, sin), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 0));
                if (innerSecond) {
                    _binormals.append(-binormal, lerpedScaledBinormal);
                } else {
                    _binormals.append(-lerpedScaledBinormal, binormal);
                }

                if (innerSecond) {
                    _indices.append(innerIndex, lastRimIndex, i1 + 0);
                } else {
                    _indices.append(lastRimIndex, innerIndex, i1 + 1);
                }
                if (fanTriangles > 1) {
                    // ... and the same sliver on the outgoing quad's chord.
                    if (innerSecond) {
                        _indices.append(innerIndex, i1 + 0, i1 + 1);
                    } else {
                        _indices.append(i1 + 1, innerIndex, i1 + 0);
                    }
                }
            }
            else {
                // Use miter line join
                cglib::vec2<float> lerpedBinormal = cglib::unit(binormal + prevBinormal);
                std::int8_t sin = static_cast<std::int8_t>(127.0f * cglib::dot_product(prevTangent, lerpedBinormal));
                cglib::vec2<float> lerpedScaledBinormal = lerpedBinormal * (1 / std::sqrt((1 + dot) * 0.5f));

                _coords.append(p0, p0);
                _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
                _binormals.append(-lerpedScaledBinormal, lerpedScaledBinormal);
                _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, -sin), cglib::vec4<std::int8_t>(styleIndex, 0, -1, sin));

                if (stroke) {
                    _coords.append(p0, p0);
                    _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
                    _binormals.append(-lerpedScaledBinormal, lerpedScaledBinormal);
                    _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, sin), cglib::vec4<std::int8_t>(styleIndex, 0, -1, -sin));
                }
            }
        }
            
        {
            const cglib::vec2<float>& p0 = points[i - 1];
            float u0 = linePos * du_dl;

            std::size_t i0 = _coords.size();
            
            cglib::bbox2<float> bounds(p0);
            bounds.add(_coords[i0 - 2]);
            if (_clipBox.inside(bounds)) {
                _indices.append(i0 - 1, i0 - 2, i0 + 0);
                _indices.append(i0 - 1, i0 + 0, i0 + 1);
            }

            // An arrow head replaces the cap and pulls the line's last vertices back by its own
            // length, so the line stops where the head starts instead of poking out of it. The
            // pull-back rides the binormal attribute - the shader multiplies it by the line width,
            // so it is the same screen-space offset the extrusion uses, which the tile coordinates
            // here can not express (the head's size is in pixels, not in metres).
            bool endArrow = !cycle && style.hasEndArrow();
            cglib::vec2<float> setback = endArrow ? tangent * lineEndArrowInradius(style) : cglib::vec2<float>(0, 0);

            _coords.append(p0, p0);
            _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
            _binormals.append(-binormal - setback, binormal - setback);
            _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 0), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 0));

            if (endArrow) {
                tesselateLineEndArrow(p0, u0, v0, v1, tangent, binormal, styleIndex, style);
            }
            else if (endpoints) {
                std::size_t i1 = _coords.size();
                tesselateLineEndPoint(p0, u0, v0, v1, i1, i0, tangent, binormal, styleIndex, style);
            }
        }
        return true;
    }

    float TileLayerBuilder::lineEndArrowInradius(const LineStyle& style) {
        // Sizes are multiples of the LINE WIDTH; one binormal unit is half a line width, so a base
        // of endArrowWidth line widths is endArrowWidth units to each side, and a length of
        // endArrowLength line widths is 2 * endArrowLength units.
        float halfBase = style.endArrowWidth;
        float length = 2.0f * style.endArrowLength;
        return halfBase * length / (halfBase + std::sqrt(halfBase * halfBase + length * length));
    }

    // A CUSTOM head outline, offset outward by one unit - half a line width - with miter joins.
    // For the built-in triangle this is the same thing as growing it about its incenter, which is
    // why both look identical; on any other contour the homothety would stop keeping the edges an
    // equal distance apart and only the per-edge offset stays right. The miter is left unclamped:
    // clamping bevels the tip, and a maneuver arrow is read by its point. A concave contour can
    // self-intersect here - that is the known limit of offsetting a polygon this cheaply.
    std::vector<cglib::vec2<float>> TileLayerBuilder::lineEndArrowShapeOutline(const LineStyle& style) {
        const std::vector<cglib::vec2<float>>& shape = *style.endArrowShape;
        std::size_t n = shape.size();
        std::vector<cglib::vec2<float>> skeleton;
        skeleton.reserve(n);
        // MEASURED, not assumed: the line's own edge sits TWO binormal units from its centre, so a
        // unit is a quarter of the line width. A path coordinate is documented as one line width,
        // hence the four, and the outward offset below is two units - half a line width, the same
        // distance a casing rule puts between its edge and the fill's.
        constexpr float UNITS_PER_LINE_WIDTH = 4.0f;
        constexpr float OFFSET_UNITS = UNITS_PER_LINE_WIDTH * 0.5f;
        for (const cglib::vec2<float>& vertex : shape) {
            skeleton.emplace_back(vertex(0) * UNITS_PER_LINE_WIDTH, vertex(1) * UNITS_PER_LINE_WIDTH);
        }

        double area = 0;
        for (std::size_t i = 0; i < n; i++) {
            const cglib::vec2<float>& p = skeleton[i];
            const cglib::vec2<float>& q = skeleton[(i + 1) % n];
            area += static_cast<double>(p(0)) * q(1) - static_cast<double>(q(0)) * p(1);
        }
        float winding = area < 0 ? -1.0f : 1.0f;

        std::vector<cglib::vec2<float>> outline;
        outline.reserve(n);
        for (std::size_t i = 0; i < n; i++) {
            const cglib::vec2<float>& prev = skeleton[(i + n - 1) % n];
            const cglib::vec2<float>& cur = skeleton[i];
            const cglib::vec2<float>& next = skeleton[(i + 1) % n];
            cglib::vec2<float> d0 = cglib::unit(cur - prev), d1 = cglib::unit(next - cur);
            cglib::vec2<float> n0(d0(1) * winding, -d0(0) * winding), n1(d1(1) * winding, -d1(0) * winding);
            cglib::vec2<float> bisector = n0 + n1;
            float len = cglib::length(bisector);
            if (len < 1.0e-6f) {
                outline.push_back(cur + n0 * OFFSET_UNITS);
                continue;
            }
            bisector = bisector * (1.0f / len);
            // A REFLEX vertex - the inside of a swallow tail's notch - has to be clamped: its miter
            // runs away from the contour instead of along it, and an unclamped one folds the outline
            // over itself. Convex corners keep the exact miter, which is what keeps a tip sharp.
            float turn = (d0(0) * d1(1) - d0(1) * d1(0)) * winding;
            float miter = 1.0f / std::max(1.0e-2f, cglib::dot_product(bisector, n0));
            outline.push_back(cur + bisector * OFFSET_UNITS * (turn < 0 ? std::min(miter, 1.0f) : miter));
        }
        return outline;
    }

    bool TileLayerBuilder::tesselateLineEndArrow(const cglib::vec2<float>& p0, float u0, float v0, float v1, const cglib::vec2<float>& tangent, const cglib::vec2<float>& binormal, std::int8_t styleIndex, const LineStyle& style) {
        if (!_clipBox.inside(p0)) {
            return false;
        }
        if (style.endArrowShape && style.endArrowShape->size() >= 3) {
            return tesselateLineEndArrowShape(p0, u0, v0, v1, tangent, binormal, styleIndex, style);
        }

        float halfBase = style.endArrowWidth;
        float length = 2.0f * style.endArrowLength;
        // The head hangs on its INCENTER, not on its tip or its base. Every offset here is a
        // multiple of the line width, so a casing rule draws the same triangle a few units bigger
        // about the same incenter - and a homothety about the incenter moves every edge by the
        // SAME distance. Anchoring the tip instead pins the two triangles together at the point:
        // the border is then zero at the tip and widest at the base, which reads as a wedge.
        float inradius = lineEndArrowInradius(style);
        cglib::vec2<float> base = -tangent * inradius;
        cglib::vec2<float> tip = tangent * (length - inradius);

        // The head is solid: its corners carry a zero antialias distance, so the fragment shader
        // keeps them opaque. The distance field a line uses describes a band one width wide, not a
        // triangle - stretched over this one it faded the silhouette near the barbs while leaving
        // the tip hard.
        const cglib::vec4<std::int8_t> attrib(styleIndex, 0, 0, 0);
        std::size_t i0 = _coords.size();

        // A head drawn on its own has a SLOT cut out of its base, one line width wide - the width
        // of the very line this rule draws elsewhere. It is what lets a style put the head OVER the
        // shaft and still read as one polygon: the slot leaves the shaft it docks on untouched, so
        // no bar of head colour crosses the line, while everything outside the slot - the shoulders
        // beside the shaft, the barbs, the tip - paints over it and keeps the arrow's silhouette
        // where the head lies on its own line, a U-turn seen from far enough away.
        if (style.endArrowOnly && halfBase > 1.0f) {
            cglib::vec2<float> slot = binormal;
            _coords.append(p0, p0, p0);
            _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, (v0 + v1) * 0.5f));
            _binormals.append(base + binormal * halfBase, base + slot, slot);
            _attribs.append(attrib, attrib, attrib);

            _coords.append(p0, p0, p0);
            _texCoords.append(cglib::vec2<float>(u0, (v0 + v1) * 0.5f), cglib::vec2<float>(u0, v1), cglib::vec2<float>(u0, v1));
            _binormals.append(-slot, base - slot, base - binormal * halfBase);
            _attribs.append(attrib, attrib, attrib);

            _coords.append(p0);
            _texCoords.append(cglib::vec2<float>(u0, (v0 + v1) * 0.5f));
            _binormals.append(tip);
            _attribs.append(attrib);

            // A fan from the tip: the slotted head is still star-shaped from it.
            std::size_t tipIndex = i0 + 6;
            for (std::size_t k = 0; k + 1 < 6; k++) {
                _indices.append(tipIndex, i0 + k, i0 + k + 1);
            }
            return true;
        }

        _coords.append(p0, p0);
        _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
        _binormals.append(base + binormal * halfBase, base - binormal * halfBase);
        _attribs.append(attrib, attrib);

        _coords.append(p0);
        _texCoords.append(cglib::vec2<float>(u0, (v0 + v1) * 0.5f));
        _binormals.append(tip);
        _attribs.append(attrib);

        _indices.append(i0 + 0, i0 + 1, i0 + 2);
        return true;
    }

    namespace {
        // Sutherland-Hodgman against one half plane, keeping nx*x + ny*y >= d. Correct on a concave
        // polygon too - a half plane can not split one into pieces, only dent it.
        std::vector<cglib::vec2<float>> clipHalfPlane(const std::vector<cglib::vec2<float>>& poly, float nx, float ny, float d) {
            std::vector<cglib::vec2<float>> result;
            result.reserve(poly.size() + 2);
            for (std::size_t i = 0; i < poly.size(); i++) {
                const cglib::vec2<float>& cur = poly[i];
                const cglib::vec2<float>& next = poly[(i + 1) % poly.size()];
                float dc = nx * cur(0) + ny * cur(1) - d;
                float dn = nx * next(0) + ny * next(1) - d;
                if (dc >= 0) {
                    result.push_back(cur);
                }
                if ((dc >= 0) != (dn >= 0)) {
                    float t = dc / (dc - dn);
                    result.push_back(cur + (next - cur) * t);
                }
            }
            return result;
        }

        // Ear clipping: the head can be concave - a swallow tail, a chevron with a notch - and a fan
        // from one vertex would fill exactly the dent that makes the shape what it is.
        void earClip(const std::vector<cglib::vec2<float>>& poly, std::vector<std::array<std::size_t, 3>>& triangles) {
            std::size_t n = poly.size();
            if (n < 3) {
                return;
            }
            double area = 0;
            for (std::size_t i = 0; i < n; i++) {
                const cglib::vec2<float>& p = poly[i];
                const cglib::vec2<float>& q = poly[(i + 1) % n];
                area += static_cast<double>(p(0)) * q(1) - static_cast<double>(q(0)) * p(1);
            }
            float winding = area < 0 ? -1.0f : 1.0f;

            std::vector<std::size_t> remaining(n);
            for (std::size_t i = 0; i < n; i++) {
                remaining[i] = i;
            }
            auto cross = [&poly, winding](std::size_t a, std::size_t b, std::size_t c) {
                cglib::vec2<float> u = poly[b] - poly[a], v = poly[c] - poly[a];
                return (u(0) * v(1) - u(1) * v(0)) * winding;
            };
            // A flattened curve leaves runs of nearly straight points: an ear whose area is a
            // rounding error is not a failure, it is just flat, so it is clipped rather than
            // refused. Refusing them is what left holes in the head.
            std::size_t guard = 0;
            while (remaining.size() > 3 && guard++ < n * n * 2) {
                bool clipped = false;
                for (std::size_t k = 0; k < remaining.size(); k++) {
                    std::size_t a = remaining[(k + remaining.size() - 1) % remaining.size()];
                    std::size_t b = remaining[k];
                    std::size_t c = remaining[(k + 1) % remaining.size()];
                    if (cross(a, b, c) < -1.0e-6f) {
                        continue; // reflex, not an ear
                    }
                    bool empty = true;
                    for (std::size_t other : remaining) {
                        if (other == a || other == b || other == c) {
                            continue;
                        }
                        if (cross(a, b, other) > 1.0e-6f && cross(b, c, other) > 1.0e-6f && cross(c, a, other) > 1.0e-6f) {
                            empty = false;
                            break;
                        }
                    }
                    if (!empty) {
                        continue;
                    }
                    triangles.push_back({ a, b, c });
                    remaining.erase(remaining.begin() + k);
                    clipped = true;
                    break;
                }
                if (!clipped) {
                    break; // degenerate contour: keep what was clipped, drop the rest
                }
            }
            if (remaining.size() == 3) {
                triangles.push_back({ remaining[0], remaining[1], remaining[2] });
            }
        }
    }

    bool TileLayerBuilder::tesselateLineEndArrowShape(const cglib::vec2<float>& p0, float u0, float v0, float v1, const cglib::vec2<float>& tangent, const cglib::vec2<float>& binormal, std::int8_t styleIndex, const LineStyle& style) {
        std::vector<cglib::vec2<float>> outline = lineEndArrowShapeOutline(style);
        if (outline.size() < 3) {
            return false;
        }

        // The head minus its docking slot, as three convex clips rather than a polygon subtraction:
        // what is in front of the base, plus each shoulder beside the shaft. Their union is the head
        // with a slot one line width wide - the shaft it docks on stays untouched, so no bar of head
        // colour crosses the line, and everything else still paints over it.
        // No docking slot here, unlike the built-in triangle: a slot is a notch where the shaft
        // enters the head, and a custom contour is placed AHEAD of the line end rather than
        // straddling it - cutting one through the middle of an icon only gashes it.
        std::vector<std::vector<cglib::vec2<float>>> pieces { outline };

        const cglib::vec4<std::int8_t> attrib(styleIndex, 0, 0, 0);
        bool drawn = false;
        for (const std::vector<cglib::vec2<float>>& piece : pieces) {
            if (piece.size() < 3) {
                continue;
            }
            std::vector<std::array<std::size_t, 3>> triangles;
            earClip(piece, triangles);
            if (triangles.empty()) {
                continue;
            }
            // Solid, with a zero antialias distance: the distance field a line carries describes a
            // band one width wide, not a head, and stretched over one it eats the silhouette.
            std::size_t i0 = _coords.size();
            for (const cglib::vec2<float>& vertex : piece) {
                _coords.append(p0);
                _texCoords.append(cglib::vec2<float>(u0, (v0 + v1) * 0.5f));
                _binormals.append(tangent * vertex(0) + binormal * vertex(1));
                _attribs.append(attrib);
            }
            for (const std::array<std::size_t, 3>& triangle : triangles) {
                _indices.append(i0 + triangle[0], i0 + triangle[1], i0 + triangle[2]);
            }
            drawn = true;
        }
        return drawn;
    }

    bool TileLayerBuilder::tesselateLineEndPoint(const cglib::vec2<float>& p0, float u0, float v0, float v1, std::size_t i0, std::size_t i1, const cglib::vec2<float>& tangent, const cglib::vec2<float>& binormal, std::int8_t styleIndex, const LineStyle& style) {
        if (_clipBox.inside(p0)) {
            float cap = style.capMode == LineCapMode::ROUND ? 1.0f : 0.0f;

            _coords.append(p0, p0);
            _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
            _binormals.append(tangent - binormal, tangent + binormal);
            _attribs.append(cglib::vec4<std::int8_t>(styleIndex, cap, 1, 0), cglib::vec4<std::int8_t>(styleIndex, cap, -1, 0));

            _indices.append(i0 + 1, i1 + 0, i0 + 0);
            _indices.append(i0 + 1, i1 + 1, i1 + 0);
        }
        return true;
    }
}

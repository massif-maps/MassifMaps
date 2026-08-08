#include "TileLayerBuilder.h"

#include <map>
#include <mutex>
#include <cmath>
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

    static float calculateScale(const carto::vt::VertexArray<float>& values, const carto::vt::VertexArray<std::size_t>& indices) {
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
    static float calculateScale(const carto::vt::VertexArray<T>& values, const carto::vt::VertexArray<std::size_t>& indices) {
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
    std::shared_ptr<const carto::vt::Bitmap> buildRoundedRectBitmap(int radius) {
        static std::mutex mutex;
        static std::map<int, std::shared_ptr<const carto::vt::Bitmap>> cache;
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
        auto bitmap = std::make_shared<carto::vt::Bitmap>(size, size, std::move(data));
        cache[radius] = bitmap;
        return bitmap;
    }
}

namespace carto::vt {
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

        if (_builderParameters.pattern != style.pattern || _builderParameters.translate != translate || _builderParameters.compOp != style.compOp || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        else if (!(_builderParameters.type == TileGeometry::Type::POLYGON || (_builderParameters.type == TileGeometry::Type::LINE && !style.pattern))) { // we can use also line drawing shader but ONLY if pattern is not used for polygons (pattern can be used for lines)
            appendGeometry();
        }
        else {
            type = _builderParameters.type;
        }
        _builderParameters.type = type;
        _builderParameters.pattern = style.pattern;
        _builderParameters.translate = translate;
        _builderParameters.compOp = style.compOp;
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc && _builderParameters.widthFuncs[styleIndex] == FloatFunction(0) && _builderParameters.offsetFuncs[styleIndex] == FloatFunction(0) && _builderParameters.lineStrokeIds[styleIndex] == 0) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
            _builderParameters.widthFuncs[styleIndex] = FloatFunction(0); // fill width information when we need to use line shader with polygons
            _builderParameters.offsetFuncs[styleIndex] = FloatFunction(0); // fill offset information when we need to use line shader with polygons
            _builderParameters.lineStrokeIds[styleIndex] = 0; // fill stroke information when we need to use line shader with polygons
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
            _ids.fill(id, _indices.size() - _ids.size());
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

        if (!_labelStyle || _labelStyle->orientation != style.orientation || _labelStyle->colorFunc != style.colorFunc || _labelStyle->sizeFunc != style.sizeFunc || _labelStyle->haloColorFunc != ColorFunction() || _labelStyle->haloRadiusFunc != FloatFunction() || _labelStyle->autoflip != style.autoflip || _labelStyle->scale != scale || _labelStyle->ascent != 0.0f || _labelStyle->descent != 0.0f || _labelStyle->transform != transform || _labelStyle->glyphMap != glyphMap || _labelStyle->maxDistance != style.maxDistance) {
            _labelStyle = std::make_shared<TileLabel::Style>(style.orientation, style.colorFunc, style.sizeFunc, ColorFunction(), FloatFunction(), style.autoflip, scale, 0.0f, 0.0f, transform, glyphMap, 27, style.maxDistance);
        }

        return [bitmapGlyphs, this](long long id, long long labelId, long long groupId, const std::variant<Vertex, Vertices>& position, float priority, float minimumGroupDistance, bool allowOverlapSameFeatureId, bool sameFeatureIdDependent, int geoPointIndex) {
            std::optional<cglib::vec2<float>> labelPosition;
            std::vector<cglib::vec2<float>> labelVertices;
            if (auto pos = std::get_if<Vertex>(&position)) {
                labelPosition = *pos;
            }
            else if (auto vertices = std::get_if<Vertices>(&position)) {
                VertexArray<cglib::vec2<float>> tesselatedVertices;
                _transformer->tesselateLineString(vertices->data(), vertices->size(), tesselatedVertices);
                labelVertices.assign(tesselatedVertices.begin(), tesselatedVertices.end());
            }

            TileLabel::PlacementInfo placementInfo(priority, minimumGroupDistance, allowOverlapSameFeatureId, sameFeatureIdDependent);
            long long globalId = (labelId ^ (static_cast<long long>(_layerIdx) << 32)) * 3 + 0;
            auto pointLabel = std::make_shared<TileLabel>(id, globalId, groupId, bitmapGlyphs, std::move(labelPosition), std::move(labelVertices), _labelStyle, placementInfo, geoPointIndex);
            _labelList.push_back(std::move(pointLabel));
        };
    }

    TileLayerBuilder::TextLabelProcessor TileLayerBuilder::createTextLabelProcessor(const TextLabelStyle& style, const TextFormatter& formatter) {
        if (style.sizeFunc == FloatFunction(0) && !style.backgroundImage) {
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
            || _labelStyle->backgroundColor != style.backgroundColor
            || _labelStyle->backgroundRadius != style.backgroundRadius
            || _labelStyle->backgroundPadding != style.backgroundPadding;

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
            // The plate behind the text is 3-sliced from one atlas cell: a rounded-corner square
            // whose left and right halves are the caps and whose middle column is stretched. The
            // bitmaps are cached by radius (the glyph map dedupes by POINTER), so a style that
            // rebuilds does not grow the atlas.
            std::optional<GlyphMap::Glyph> backgroundGlyph;
            if (style.backgroundColor.value() != 0) {
                int radius = std::min(32, std::max(0, static_cast<int>(style.backgroundRadius + 0.5f)));
                if (const GlyphMap::Glyph* glyph = font->getGlyphMap()->getGlyph(font->getGlyphMap()->loadBitmapGlyph(buildRoundedRectBitmap(radius), GlyphMap::GlyphMode::BITMAP))) {
                    backgroundGlyph = *glyph;
                }
            }
            _labelStyle = std::make_shared<TileLabel::Style>(style.orientation, style.colorFunc, style.sizeFunc, style.haloColorFunc, style.haloRadiusFunc, style.autoflip, scale, metrics.ascent, metrics.descent, transform, font->getGlyphMap(), glyphRenderSize, style.maxDistance, style.secondaryColorFunc, style.rankFunc, style.calloutScreenAnchor, style.calloutOffset, style.calloutStep, style.calloutMaxRows, style.calloutPersistPasses, style.calloutLineWidth, style.calloutLineAnchor, style.calloutBandAnchor, calloutLineGlyph, style.backgroundColor, style.backgroundRadius, style.backgroundPadding, backgroundGlyph);
        }

        return [style, font, formatter, this](long long id, long long labelId, long long groupId, const std::optional<Vertex>& position, const Vertices& vertices, const std::string& text, float priority, float minimumGroupDistance, bool allowOverlapSameFeatureId, bool sameFeatureIdDependent, int geoPointIndex) {
            if (!text.empty() || style.backgroundImage) {
                std::vector<Font::Glyph> glyphs;
                if (!text.empty()) {
                    glyphs = formatter.format(text, 1.0f);
                }
                if (style.backgroundImage) {
                    const GlyphMap::Glyph* baseGlyph = font->getGlyphMap()->getGlyph(font->getGlyphMap()->loadBitmapGlyph(style.backgroundImage->bitmap, GlyphMap::GlyphMode::BACKGROUND));
                    if (baseGlyph) {
                        float scale = style.backgroundImage->scale / formatter.getFontSize();
                        glyphs.insert(glyphs.begin(), Font::Glyph(0, Font::NULL_CODEPOINT, *baseGlyph, cglib::vec2<float>(baseGlyph->width, baseGlyph->height) * (style.backgroundScale * scale), style.backgroundOffset * scale, cglib::vec2<float>(baseGlyph->width, 0) * scale));
                    }
                }

                std::optional<cglib::vec2<float>> labelPosition;
                if (position) {
                    labelPosition = *position;
                }
                std::vector<cglib::vec2<float>> labelVertices;
                if (!vertices.empty()) {
                    VertexArray<cglib::vec2<float>> tesselatedVertices;
                    _transformer->tesselateLineString(vertices.data(), vertices.size(), tesselatedVertices);
                    labelVertices.assign(tesselatedVertices.begin(), tesselatedVertices.end());
                }

                TileLabel::PlacementInfo placementInfo(priority, minimumGroupDistance, allowOverlapSameFeatureId, sameFeatureIdDependent);
                long long globalId = (labelId ^ (static_cast<long long>(_layerIdx) << 32)) * 3 + (style.backgroundImage ? 2 : 1);
                auto textLabel = std::make_shared<TileLabel>(id, globalId, groupId, std::move(glyphs), std::move(labelPosition), std::move(labelVertices), _labelStyle, placementInfo, geoPointIndex);
                _labelList.push_back(std::move(textLabel));
            }
        };
    }

    std::shared_ptr<TileLayer> TileLayerBuilder::buildTileLayer() const {
        std::vector<std::shared_ptr<TileGeometry>> geometryList = _geometryList;
        packGeometry(geometryList);

        return std::make_shared<TileLayer>(_layerName, _layerIdx, _compOp, _opacityFunc, _backgroundList, _bitmapList, std::move(geometryList), _labelList);
    }

    void TileLayerBuilder::appendGeometry() {
        if (_builderParameters.type == TileGeometry::Type::NONE) {
            return;
        }

        packGeometry(_geometryList);

        _builderParameters = BuilderParameters();
        _coords.clear();
        _texCoords.clear();
        _binormals.clear();
        _heights.clear();
        _attribs.clear();
        _indices.clear();
        _ids.clear();
        _geoPosIndexes.clear();
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
        if (std::all_of(attribs.begin(), attribs.end(), [](const cglib::vec4<std::int8_t>& attrib) { return attrib == cglib::vec4<std::int8_t>(0, 0, 0, 0); })) {
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

        // Split/repack geometry
        float coordScale = calculateScale(coords, _indices);
        float binormalScale = calculateScale(binormals, _indices);
        float texCoordScale = calculateScale(texCoords, _indices);
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
                }

                remappedIndices.append(remappedIndex);
                remappedIds.append(_ids[offset + i]);
                remappedGeoPosIndexes.append(_geoPosIndexes[offset + i]);
            }

            packGeometry(_builderParameters.type, dimensions, coordScale, binormalScale, texCoordScale, heightScale, remappedCoords, remappedTexCoords, remappedNormals, remappedBinormals, remappedHeights, remappedAttribs, remappedIndices, remappedIds, remappedGeoPosIndexes, styleParameters,  geometryList);

            offset += count;
        }
    }

    void TileLayerBuilder::packGeometry(TileGeometry::Type type, int dimensions, float coordScale, float binormalScale, float texCoordScale, float heightScale, const VertexArray<cglib::vec3<float>>& coords, const VertexArray<cglib::vec2<float>>& texCoords, const VertexArray<cglib::vec3<float>>& normals, const VertexArray<cglib::vec3<float>>& binormals, const VertexArray<float>& heights, const VertexArray<cglib::vec4<std::int8_t>>& attribs, const VertexArray<std::size_t>& indices, const VertexArray<long long>& ids, const VertexArray<std::uint16_t>& geoPosIndexes, const TileGeometry::StyleParameters& styleParameters, std::vector<std::shared_ptr<TileGeometry>>& geometryList) const {
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

    bool TileLayerBuilder::tesselatePolygon3D(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float minHeight, float maxHeight, std::int8_t styleIndex, const Polygon3DStyle& style) {
        _tesselator.clear();
        if (!_tesselator.tesselate(pointsList)) {
            return false;
        }

        if (minHeight != maxHeight) {
            for (const std::vector<cglib::vec2<float>>& points : pointsList) {
                std::size_t j = points.size() - 1;
                for (std::size_t i = 0; i < points.size(); i++) {
                    cglib::bbox2<float> bounds(points[i]);
                    bounds.add(points[j]);
                    if (_polygonClipBox.inside(bounds)) {
                        cglib::vec2<float> tangent(cglib::unit(points[i] - points[j]));
                        cglib::vec2<float> binormal = cglib::vec2<float>(tangent(1), -tangent(0));

                        std::size_t i0 = _coords.size();
                        _coords.append(points[i], points[j], points[j]);
                        _texCoords.append(points[i], points[j], points[j]);
                        _binormals.append(binormal, binormal, binormal);
                        _heights.append(minHeight, minHeight, maxHeight);
                        _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 1, 0, 0), cglib::vec4<std::int8_t>(styleIndex, 1, 0, 0), cglib::vec4<std::int8_t>(styleIndex, 1, 1, 0));
                        _indices.append(i0 + 0, i0 + 1, i0 + 2);

                        std::size_t i1 = _coords.size();
                        _coords.append(points[j], points[i], points[i]);
                        _texCoords.append(points[j], points[i], points[i]);
                        _binormals.append(binormal, binormal, binormal);
                        _heights.append(maxHeight, maxHeight, minHeight);
                        _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 1, 1, 0), cglib::vec4<std::int8_t>(styleIndex, 1, 1, 0), cglib::vec4<std::int8_t>(styleIndex, 1, 0, 0));
                        _indices.append(i1 + 0, i1 + 1, i1 + 2);
                    }

                    j = i;
                }
            }
        }

        std::size_t offset = _coords.size();
        for (std::size_t i = 0; i < _tesselator.getVertices().size(); i++) {
            cglib::vec2<float> p = _tesselator.getVertices()[i];

            _coords.append(p);
            _texCoords.append(p);
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
        _attribs.fill(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 0), _coords.size() - offset);

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

            _coords.append(p0, p0);
            _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
            _binormals.append(-binormal, binormal);
            _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 0), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 0));

            if (endpoints) {
                std::size_t i1 = _coords.size();
                tesselateLineEndPoint(p0, u0, v0, v1, i1, i0, tangent, binormal, styleIndex, style);
            }
        }
        return true;
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

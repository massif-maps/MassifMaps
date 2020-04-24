#include "TileLayerBuilder.h"
#include "TextFormatter.h"
#include "Color.h"

#include <utility>
#include <algorithm>
#include <iterator>

#include <boost/math/constants/constants.hpp>

#include <tesselator.h>

namespace {
    static float calculateScale(const carto::vt::VertexArray<float>& values) {
        float maxValue = 0.0f;
        for (float value : values) {
            maxValue = std::max(maxValue, std::abs(value));
        }
        if (maxValue == 0.0f) {
            return 1.0f;
        }
        return std::pow(2.0f, std::floor(std::log(32767.0f / maxValue) / std::log(2.0f)));
    }

    template <typename T>
    static float calculateScale(const carto::vt::VertexArray<T>& values) {
        float maxValue = 0.0f;
        for (const T& value : values) {
            for (auto it = value.cbegin(); it != value.cend(); it++) {
                maxValue = std::max(maxValue, std::abs(static_cast<float>(*it)));
            }
        }
        if (maxValue == 0.0f) {
            return 1.0f;
        }
        return std::pow(2.0f, std::floor(std::log(32767.0f / maxValue) / std::log(2.0f)));
    }
}

namespace carto { namespace vt {
    TileLayerBuilder::TileLayerBuilder(const TileId& tileId, int layerIdx, std::shared_ptr<const TileTransformer::VertexTransformer> transformer, float tileSize, float geomScale) :
        _tileId(tileId), _layerIdx(layerIdx), _tileSize(tileSize), _geomScale(geomScale), _transformer(std::move(transformer)), _clipBox(cglib::vec2<float>(-0.1f, -0.1f), cglib::vec2<float>(1.1f, 1.1f))
    {
        _coords.reserve(RESERVED_VERTICES);
        _texCoords.reserve(RESERVED_VERTICES);
        _binormals.reserve(RESERVED_VERTICES);
        _heights.reserve(RESERVED_VERTICES);
        _attribs.reserve(RESERVED_VERTICES);
        _indices.reserve(RESERVED_VERTICES);
        _ids.reserve(RESERVED_VERTICES);
    }

    void TileLayerBuilder::setClipBox(const cglib::bbox2<float>& clipBox) {
        _clipBox = clipBox;
    }

    void TileLayerBuilder::addBitmap(const std::shared_ptr<TileBitmap>& bitmap) {
        _bitmapList.push_back(bitmap);
    }

    void TileLayerBuilder::addPoints(const std::function<bool(long long& id, Vertex& vertex)>& generator, const PointStyle& style, const std::shared_ptr<GlyphMap>& glyphMap) {
        if (style.sizeFunc == FloatFunction(0) || !style.image) {
            return;
        }
        
        long long id = 0;
        Vertex vertex(0, 0);
        if (!generator(id, vertex)) {
            return;
        }

        if (_builderParameters.type != TileGeometry::Type::POINT || _builderParameters.glyphMap != glyphMap || _builderParameters.transform != style.transform || _builderParameters.compOp != style.compOp || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        _builderParameters.type = TileGeometry::Type::POINT;
        _builderParameters.glyphMap = glyphMap;
        _builderParameters.transform = style.transform;
        _builderParameters.compOp = style.compOp;
        GlyphMap::GlyphId glyphId = glyphMap->loadBitmapGlyph(style.image->bitmap, style.image->sdfMode);
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc && _builderParameters.widthFuncs[styleIndex] == style.sizeFunc && _builderParameters.strokeWidthFuncs[styleIndex] == FloatFunction(0)) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
            _builderParameters.widthFuncs[styleIndex] = style.sizeFunc;
            _builderParameters.strokeWidthFuncs[styleIndex] = FloatFunction(0);
        }
        
        do {
            std::size_t i0 = _indices.size();
            cglib::vec2<float> pen(0, 0);
            const GlyphMap::Glyph* glyph = glyphMap->getGlyph(glyphId);
            if (glyph) {
                pen = -cglib::vec2<float>(glyph->width, glyph->height) * 0.5f;
                tesselateGlyph(vertex, static_cast<std::int8_t>(styleIndex), pen * style.image->scale, cglib::vec2<float>(glyph->width, glyph->height) * style.image->scale, glyph);
            }
            _ids.fill(id, _indices.size() - i0);
        } while (generator(id, vertex));
    }

    void TileLayerBuilder::addTexts(const std::function<bool(long long& id, Vertex& vertex, std::string& text)>& generator, const TextStyle& style, const TextFormatter& formatter) {
        if (style.sizeFunc == FloatFunction(0) && !style.backgroundImage) {
            return;
        }
        
        long long id = 0;
        Vertex vertex(0, 0);
        std::string text;
        if (!generator(id, vertex, text)) {
            return;
        }

        boost::optional<cglib::mat3x3<float>> transform;
        if (style.angle != 0) {
            float angle = -style.angle * boost::math::constants::pi<float>() / 180.0f;
            transform = cglib::rotate3_matrix(angle);
        }

        const std::shared_ptr<Font>& font = formatter.getFont();

        if (_builderParameters.type != TileGeometry::Type::POINT || _builderParameters.glyphMap != font->getGlyphMap() || _builderParameters.transform != transform || _builderParameters.compOp != style.compOp || _builderParameters.parameterCount + 2 > TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        _builderParameters.type = TileGeometry::Type::POINT;
        _builderParameters.glyphMap = font->getGlyphMap();
        _builderParameters.transform = transform;
        _builderParameters.compOp = style.compOp;
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc && _builderParameters.widthFuncs[styleIndex] == style.sizeFunc && _builderParameters.strokeWidthFuncs[styleIndex] == FloatFunction(0)) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
            _builderParameters.widthFuncs[styleIndex] = style.sizeFunc;
            _builderParameters.strokeWidthFuncs[styleIndex] = FloatFunction(0);
        }

        int haloStyleIndex = -1;
        if (style.haloRadiusFunc != FloatFunction(0)) {
            for (haloStyleIndex = _builderParameters.parameterCount; --haloStyleIndex >= 0; ) {
                if (_builderParameters.colorFuncs[haloStyleIndex] == style.haloColorFunc && _builderParameters.widthFuncs[haloStyleIndex] == style.sizeFunc && _builderParameters.strokeWidthFuncs[haloStyleIndex] == style.haloRadiusFunc) {
                    break;
                }
            }
            if (haloStyleIndex < 0) {
                haloStyleIndex = _builderParameters.parameterCount++;
                _builderParameters.colorFuncs[haloStyleIndex] = style.haloColorFunc;
                _builderParameters.widthFuncs[haloStyleIndex] = style.sizeFunc;
                _builderParameters.strokeWidthFuncs[haloStyleIndex] = style.haloRadiusFunc;
            }
        }

        do {
            std::size_t i0 = _indices.size();
            std::size_t i1 = _binormals.size();
            std::vector<Font::Glyph> glyphs = formatter.format(text, 1.0f);
            Font::Metrics metrics = font->getMetrics(1.0f);
            if (style.backgroundImage) {
                const GlyphMap::Glyph* baseGlyph = font->getGlyphMap()->getGlyph(font->getGlyphMap()->loadBitmapGlyph(style.backgroundImage->bitmap, style.backgroundImage->sdfMode));
                if (baseGlyph) {
                    float scale = style.backgroundImage->scale / formatter.getFontSize();
                    Font::Glyph glyph(0, *baseGlyph, cglib::vec2<float>(baseGlyph->width, baseGlyph->height) * (style.backgroundScale * scale), style.backgroundOffset * scale, cglib::vec2<float>(0, 0));
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
            _ids.fill(id, _indices.size() - i0);
        } while (generator(id, vertex, text));
    }

    void TileLayerBuilder::addLines(const std::function<bool(long long& id, Vertices& vertices)>& generator, const LineStyle& style, const std::shared_ptr<StrokeMap>& strokeMap) {
        if (style.widthFunc == FloatFunction(0)) {
            return;
        }
        
        long long id = 0;
        Vertices vertices;
        if (!generator(id, vertices)) {
            return;
        }

        if ((_builderParameters.strokeMap && _builderParameters.strokeMap != strokeMap) || _builderParameters.transform != style.transform || _builderParameters.compOp != style.compOp || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        else if (!(_builderParameters.type == TileGeometry::Type::LINE || (_builderParameters.type == TileGeometry::Type::POLYGON && !_builderParameters.pattern && !_builderParameters.transform))) { // we can use also line drawing shader but ONLY if pattern/transform is not used for polygons (pattern can be used for lines)
            appendGeometry();
        }
        _builderParameters.type = TileGeometry::Type::LINE;
        _builderParameters.strokeMap = strokeMap;
        _builderParameters.transform = style.transform;
        _builderParameters.compOp = style.compOp;
        StrokeMap::StrokeId strokeId = (style.strokePattern ? strokeMap->loadBitmapPattern(style.strokePattern) : 0);
        const StrokeMap::Stroke* stroke = (strokeId != 0 ? strokeMap->getStroke(strokeId) : nullptr);
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc && _builderParameters.widthFuncs[styleIndex] == style.widthFunc && _builderParameters.lineStrokeIds[styleIndex] == strokeId) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
            _builderParameters.widthFuncs[styleIndex] = style.widthFunc;
            _builderParameters.lineStrokeIds[styleIndex] = strokeId;
        }
        
        do {
            std::size_t i0 = _indices.size();
            _binormals.fill(cglib::vec2<float>(0, 0), _coords.size() - _binormals.size()); // needed if previously only polygons were used
            tesselateLine(vertices, static_cast<std::int8_t>(styleIndex), stroke, style);
            _ids.fill(id, _indices.size() - i0);
        } while (generator(id, vertices));
    }

    void TileLayerBuilder::addPolygons(const std::function<bool(long long& id, VerticesList& verticesList)>& generator, const PolygonStyle& style) {
        long long id = 0;
        VerticesList verticesList;
        if (!generator(id, verticesList)) {
            return;
        }

        TileGeometry::Type type = TileGeometry::Type::POLYGON;
        if (_builderParameters.pattern != style.pattern || _builderParameters.transform != style.transform || _builderParameters.compOp != style.compOp || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        else if (!(_builderParameters.type == TileGeometry::Type::POLYGON || (_builderParameters.type == TileGeometry::Type::LINE && !style.pattern && !style.transform))) { // we can use also line drawing shader but ONLY if pattern/transform is not used for polygons (pattern can be used for lines)
            appendGeometry();
        }
        else {
            type = _builderParameters.type;
        }
        _builderParameters.type = type;
        _builderParameters.pattern = style.pattern;
        _builderParameters.transform = style.transform;
        _builderParameters.compOp = style.compOp;
        int styleIndex = _builderParameters.parameterCount;
        while (--styleIndex >= 0) {
            if (_builderParameters.colorFuncs[styleIndex] == style.colorFunc && _builderParameters.widthFuncs[styleIndex] == FloatFunction(0) && _builderParameters.lineStrokeIds[styleIndex] == 0) {
                break;
            }
        }
        if (styleIndex < 0) {
            styleIndex = _builderParameters.parameterCount++;
            _builderParameters.colorFuncs[styleIndex] = style.colorFunc;
            _builderParameters.widthFuncs[styleIndex] = FloatFunction(0); // fill width information when we need to use line shader with polygons
            _builderParameters.lineStrokeIds[styleIndex] = 0; // fill stroke information when we need to use line shader with polygons
        }

        do {
            std::size_t i0 = _ids.size();
            tesselatePolygon(verticesList, static_cast<std::int8_t>(styleIndex), style);
            _ids.fill(id, _indices.size() - i0);
            if (type == TileGeometry::Type::LINE) {
                _binormals.fill(cglib::vec2<float>(0, 0), _coords.size() - _binormals.size()); // use zero binormals if using 'lines'
            }
        } while (generator(id, verticesList));
    }

    void TileLayerBuilder::addPolygons3D(const std::function<bool(long long& id, VerticesList& verticesList)>& generator, float minHeight, float maxHeight, const Polygon3DStyle& style) {
        long long id = 0;
        VerticesList verticesList;
        if (!generator(id, verticesList)) {
            return;
        }

        if (_builderParameters.type != TileGeometry::Type::POLYGON3D || _builderParameters.transform != style.transform || _builderParameters.parameterCount >= TileGeometry::StyleParameters::MAX_PARAMETERS) {
            appendGeometry();
        }
        _builderParameters.type = TileGeometry::Type::POLYGON3D;
        _builderParameters.transform = style.transform;
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

        do {
            std::size_t i0 = _ids.size();
            tesselatePolygon3D(verticesList, minHeight, maxHeight, static_cast<std::int8_t>(styleIndex), style);
            _ids.fill(id, _indices.size() - i0);
        } while (generator(id, verticesList));
    }

    void TileLayerBuilder::addPointLabels(const std::function<bool(long long& id, PointLabelInfo& labelInfo)>& generator, const PointLabelStyle& style, const std::shared_ptr<GlyphMap>& glyphMap) {
        if (style.sizeFunc == FloatFunction(0) || !style.image) {
            return;
        }

        const GlyphMap::Glyph* baseGlyph = glyphMap->getGlyph(glyphMap->loadBitmapGlyph(style.image->bitmap, style.image->sdfMode));
        if (!baseGlyph) {
            return;
        }
        std::vector<Font::Glyph> bitmapGlyphs = {
            Font::Glyph(Font::CR_CODEPOINT, GlyphMap::Glyph(false, 0, 0, 0, 0, cglib::vec2<float>(0, 0)), cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0), -cglib::vec2<float>(style.image->bitmap->width, style.image->bitmap->height) * (style.image->scale * 0.5f)),
            Font::Glyph(0, *baseGlyph, cglib::vec2<float>(baseGlyph->width, baseGlyph->height) * style.image->scale, cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 0))
        };

        float scale = 1.0f / _tileSize;
        boost::optional<cglib::mat2x2<float>> transform;
        boost::optional<cglib::vec2<float>> translate;
        if (style.transform) {
            cglib::mat3x3<float> flippedTransform = (*style.transform) * cglib::scale3_matrix(cglib::vec3<float>(1, -1, 1));
            transform = cglib::mat2x2<float> {{ flippedTransform(0, 0), flippedTransform(0, 1)}, { -flippedTransform(1, 0), -flippedTransform(1, 1) }};
            translate = cglib::vec2<float>(flippedTransform(0, 2), flippedTransform(1, 2)) * (1.0f / _tileSize);
        }

        if (!_labelStyle || _labelStyle->orientation != style.orientation || _labelStyle->colorFunc != style.colorFunc || _labelStyle->sizeFunc != style.sizeFunc || _labelStyle->haloColorFunc != ColorFunction() || _labelStyle->haloRadiusFunc != FloatFunction() || _labelStyle->autoflip != style.autoflip || _labelStyle->scale != scale || _labelStyle->ascent != 0.0f || _labelStyle->descent != 0.0f || _labelStyle->transform != transform || _labelStyle->translate != translate || _labelStyle->glyphMap != glyphMap) {
            _labelStyle = std::make_shared<TileLabel::Style>(style.orientation, style.colorFunc, style.sizeFunc, ColorFunction(), FloatFunction(), style.autoflip, scale, 0.0f, 0.0f, transform, translate, glyphMap);
        }
        
        while (true) {
            long long id = 0;
            PointLabelInfo labelInfo;
            if (!generator(id, labelInfo)) {
                break;
            }

            boost::optional<cglib::vec2<float>> labelPosition;
            std::vector<cglib::vec2<float>> labelVertices;
            if (auto pos = boost::get<Vertex>(&labelInfo.position)) {
                labelPosition = *pos;
            }
            else if (auto vertices = boost::get<Vertices>(&labelInfo.position)) {
                VertexArray<cglib::vec2<float>> tesselatedVertices;
                _transformer->tesselateLineString(vertices->data(), vertices->size(), tesselatedVertices);
                labelVertices.assign(tesselatedVertices.begin(), tesselatedVertices.end());
            }

            auto pointLabel = std::make_shared<TileLabel>(_tileId, id, labelInfo.id, labelInfo.groupId, bitmapGlyphs, std::move(labelPosition), std::move(labelVertices), _labelStyle);
            pointLabel->setMinimumGroupDistance(_tileSize * labelInfo.minimumGroupDistance);
            pointLabel->setPriority(_layerIdx);
            _labelList.push_back(std::move(pointLabel));
        }
    }

    void TileLayerBuilder::addTextLabels(const std::function<bool(long long& id, TextLabelInfo& labelInfo)>& generator, const TextLabelStyle& style, const TextFormatter& formatter) {
        if (style.sizeFunc == FloatFunction(0) && !style.backgroundImage) {
            return;
        }
        
        float scale = 1.0f / _tileSize;
        boost::optional<cglib::mat2x2<float>> transform;
        boost::optional<cglib::vec2<float>> translate;
        if (style.angle != 0) {
            float angle = style.angle * boost::math::constants::pi<float>() / 180.0f;
            transform = cglib::rotate2_matrix(angle);
        }

        const std::shared_ptr<Font>& font = formatter.getFont();
        Font::Metrics metrics = formatter.getFont()->getMetrics(1.0f);
        if (!_labelStyle || _labelStyle->orientation != style.orientation || _labelStyle->colorFunc != style.colorFunc || _labelStyle->sizeFunc != style.sizeFunc || _labelStyle->haloColorFunc != style.haloColorFunc || _labelStyle->haloRadiusFunc != style.haloRadiusFunc || _labelStyle->autoflip != style.autoflip || _labelStyle->scale != scale || _labelStyle->ascent != metrics.ascent || _labelStyle->descent != metrics.descent || _labelStyle->transform != transform || _labelStyle->translate != translate || _labelStyle->glyphMap != font->getGlyphMap()) {
            _labelStyle = std::make_shared<TileLabel::Style>(style.orientation, style.colorFunc, style.sizeFunc, style.haloColorFunc, style.haloRadiusFunc, style.autoflip, scale, metrics.ascent, metrics.descent, transform, translate, font->getGlyphMap());
        }

        while (true) {
            long long id = 0;
            TextLabelInfo labelInfo;
            if (!generator(id, labelInfo)) {
                break;
            }

            if (!labelInfo.text.empty() || style.backgroundImage) {
                std::vector<Font::Glyph> glyphs = formatter.format(labelInfo.text, 1.0f);
                if (style.backgroundImage) {
                    const GlyphMap::Glyph* baseGlyph = font->getGlyphMap()->getGlyph(font->getGlyphMap()->loadBitmapGlyph(style.backgroundImage->bitmap, style.backgroundImage->sdfMode));
                    if (baseGlyph) {
                        float scale = style.backgroundImage->scale / formatter.getFontSize();
                        glyphs.insert(glyphs.begin(), Font::Glyph(0, *baseGlyph, cglib::vec2<float>(baseGlyph->width, baseGlyph->height) * (style.backgroundScale * scale), style.backgroundOffset * scale, cglib::vec2<float>(baseGlyph->width, 0) * scale));
                    }
                }

                boost::optional<cglib::vec2<float>> labelPosition;
                if (auto pos = boost::get<Vertex>(&labelInfo.position)) {
                    labelPosition = *pos;
                }
                std::vector<cglib::vec2<float>> labelVertices;
                if (!labelInfo.vertices.empty()) {
                    VertexArray<cglib::vec2<float>> tesselatedVertices;
                    _transformer->tesselateLineString(labelInfo.vertices.data(), labelInfo.vertices.size(), tesselatedVertices);
                    labelVertices.assign(tesselatedVertices.begin(), tesselatedVertices.end());
                }

                auto textLabel = std::make_shared<TileLabel>(_tileId, id, labelInfo.id, labelInfo.groupId, std::move(glyphs), std::move(labelPosition), std::move(labelVertices), _labelStyle);
                textLabel->setMinimumGroupDistance(_tileSize * labelInfo.minimumGroupDistance);
                textLabel->setPriority(_layerIdx);
                _labelList.push_back(std::move(textLabel));
            }
        }
    }

    std::shared_ptr<TileLayer> TileLayerBuilder::buildTileLayer(boost::optional<CompOp> compOp, FloatFunction opacityFunc) const {
        std::vector<std::shared_ptr<TileGeometry>> geometryList = _geometryList;
        packGeometry(geometryList);

        return std::make_shared<TileLayer>(_layerIdx, std::move(compOp), std::move(opacityFunc), _bitmapList, std::move(geometryList), _labelList);
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
            styleParameters.strokeWidthFuncs[i] = _builderParameters.strokeWidthFuncs[i];
        }
        if (_builderParameters.transform) {
            cglib::vec2<float> translate((*_builderParameters.transform)(0, 2), (*_builderParameters.transform)(1, 2));
            if (translate != cglib::vec2<float>(0, 0)) {
                styleParameters.translate = translate * (1.0f / _tileSize);
            }
        }
        styleParameters.compOp = _builderParameters.compOp;

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
        if (!_builderParameters.transform) {
            for (std::size_t i = 0; i < _coords.size(); i++) {
                coords.append(_transformer->calculatePoint(_coords[i]));
                normals.append(_transformer->calculateNormal(_coords[i]));
                if (!_binormals.empty()) {
                    binormals.append(_transformer->calculateVector(_coords[i], _binormals[i]));
                }
            }
        }
        else {
            cglib::mat2x2<float> transform = cglib::mat2x2<float> {{ (*_builderParameters.transform)(0, 0), (*_builderParameters.transform)(0, 1) }, { (*_builderParameters.transform)(1, 0), (*_builderParameters.transform)(1, 1) }};
            cglib::mat2x2<float> invTransTransform = cglib::transpose(cglib::inverse(transform));
            for (std::size_t i = 0; i < _coords.size(); i++) {
                cglib::vec2<float> pos;
                if (_builderParameters.type == TileGeometry::Type::POINT) {
                    pos = _coords[i];
                } else {
                    pos = cglib::transform(_coords[i], transform);
                }
                coords.append(_transformer->calculatePoint(pos));
                normals.append(_transformer->calculateNormal(pos));
                if (!_binormals.empty()) {
                    cglib::vec2<float> binormal;
                    if (_builderParameters.type == TileGeometry::Type::POINT) {
                        binormal = cglib::transform(_binormals[i], transform);
                    } else {
                        binormal = cglib::unit(cglib::transform(_binormals[i], invTransTransform)) * cglib::length(_binormals[i]);
                    }
                    binormals.append(_transformer->calculateVector(pos, binormal));
                }
            }
        }
        if (std::all_of(normals.begin(), normals.end(), [](const cglib::vec3<float>& normal) { return normal(2) == 1; })) {
            normals.clear();
        }

        // Transform texture coordinates. Note that texture coordinates are also used as local tile coordinates for 3D polygons.
        VertexArray<cglib::vec2<float>> texCoords;
        if (styleParameters.pattern || _builderParameters.type == TileGeometry::Type::POLYGON3D) {
            texCoords.reserve(_texCoords.size());
            cglib::mat2x2<float> transform = cglib::mat2x2<float>::identity();
            if (styleParameters.pattern) {
                transform = cglib::mat2x2<float> {{ 1.0f / styleParameters.pattern->bitmap->width, 0 }, { 0, 1.0f / styleParameters.pattern->bitmap->height }};
            }
            else if (_builderParameters.type == TileGeometry::Type::POLYGON3D && _builderParameters.transform) {
                transform = cglib::mat2x2<float> {{ (*_builderParameters.transform)(0, 0), (*_builderParameters.transform)(0, 1) }, { (*_builderParameters.transform)(1, 0), (*_builderParameters.transform)(1, 1) }};
            }
            for (std::size_t i = 0; i < _texCoords.size(); i++) {
                texCoords.append(cglib::transform(_texCoords[i], transform));
            }
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

        // Pack geometry
        packGeometry(_builderParameters.type, dimensions, calculateScale(coords), calculateScale(binormals), calculateScale(texCoords), calculateScale(heights), coords, texCoords, normals, binormals, heights, attribs, _indices, _ids, styleParameters, geometryList);
    }

    void TileLayerBuilder::packGeometry(TileGeometry::Type type, int dimensions, float coordScale, float binormalScale, float texCoordScale, float heightScale, const VertexArray<cglib::vec3<float>>& coords, const VertexArray<cglib::vec2<float>>& texCoords, const VertexArray<cglib::vec3<float>>& normals, const VertexArray<cglib::vec3<float>>& binormals, const VertexArray<float>& heights, const VertexArray<cglib::vec4<std::int8_t>>& attribs, const VertexArray<std::size_t>& indices, const VertexArray<long long>& ids, const TileGeometry::StyleParameters& styleParameters, std::vector<std::shared_ptr<TileGeometry>>& geometryList) const {
        if (indices.empty()) {
            return;
        }
        
        // Check if we have to split indices into multiple sets (if we can not use 16-bit indices)
        if (coords.size() > 65535) {
            for (std::size_t offset = 0; offset < indices.size(); ) {
                std::size_t count = std::min(std::size_t(65535), indices.size() - offset);

                std::vector<std::size_t> indexTable(indices.size(), 65536);
                VertexArray<cglib::vec3<float>> remappedCoords;
                VertexArray<cglib::vec2<float>> remappedTexCoords;
                VertexArray<cglib::vec3<float>> remappedNormals;
                VertexArray<cglib::vec3<float>> remappedBinormals;
                VertexArray<float> remappedHeights;
                VertexArray<cglib::vec4<std::int8_t>> remappedAttribs;
                VertexArray<std::size_t> remappedIndices;
                VertexArray<long long> remappedIds;
                for (std::size_t i = 0; i < count; i++) {
                    std::size_t index = indices[offset + i];
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
                    remappedIds.append(ids[offset + i]);
                }

                packGeometry(type, dimensions, coordScale, binormalScale, texCoordScale, heightScale, remappedCoords, remappedTexCoords, remappedNormals, remappedBinormals, remappedHeights, remappedAttribs, remappedIndices, remappedIds, styleParameters, geometryList);

                offset += count;
            }
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
        VertexArray<std::uint16_t> compressedIndices;
        compressedIndices.reserve(indices.size());
        for (std::size_t i = 0; i < indices.size(); i++) {
            compressedIndices.append(static_cast<std::uint16_t>(indices[i]));
        }

        // Compress ids
        std::vector<std::pair<std::size_t, long long>> compressedIds;
        if (!ids.empty()) {
            std::size_t offset = 0;
            for (std::size_t i = 1; i < ids.size(); i++) {
                if (ids[i] != ids[offset]) {
                    compressedIds.emplace_back(i - offset, ids[offset]);
                    offset = i;
                }
            }
            compressedIds.emplace_back(ids.size() - offset, ids[offset]);
            compressedIds.shrink_to_fit();
        }

        // Store geometry
        auto geometry = std::make_shared<TileGeometry>(type, _geomScale, styleParameters, vertexGeomLayoutParams, std::move(compressedVertexGeometry), std::move(compressedIndices), std::move(compressedIds));
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
            attrib(1) = (glyph->sdfMode ? -1 : 1);
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
        if (!_tessPoolAllocator) {
            _tessPoolAllocator = std::unique_ptr<PoolAllocator>(new PoolAllocator);
        }
        _tessPoolAllocator->reset(); // reuse last allocated block from the start

        TESSalloc ma;
        memset(&ma, 0, sizeof(ma));
        ma.memalloc = [](void* userData, unsigned int size) { return reinterpret_cast<PoolAllocator*>(userData)->allocate(size); };
        ma.memfree = [](void* userData, void* ptr) {};
        ma.userData = _tessPoolAllocator.get();
        ma.extraVertices = 256; // realloc not provided, allow 256 extra vertices.

        TESStesselator* tessPtr = tessNewTess(&ma);
        if (!tessPtr) {
            return false;
        }
        std::shared_ptr<TESStesselator> tess(tessPtr, tessDeleteTess);
        
        for (const std::vector<cglib::vec2<float>>& points : pointsList) {
            TESSreal* coords = reinterpret_cast<TESSreal*>(_tessPoolAllocator->allocate(points.size() * 2 * sizeof(TESSreal)));
            for (std::size_t i = 0; i < points.size(); i++) {
                coords[i * 2 + 0] = static_cast<TESSreal>(points[i](0));
                coords[i * 2 + 1] = static_cast<TESSreal>(points[i](1));
            }
            tessAddContour(tess.get(), 2, coords, 2 * sizeof(TESSreal), static_cast<int>(points.size()));
        }
        tessTesselate(tess.get(), TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, 0);
        const TESSreal* coords = tessGetVertices(tess.get());
        const int* elements = tessGetElements(tess.get());
        int vertexCount = tessGetVertexCount(tess.get());
        int elementCount = tessGetElementCount(tess.get());

        float du_dx = 0.0f, dv_dy = 0.0f;
        if (style.pattern) {
            du_dx = _tileSize / style.pattern->widthScale;
            dv_dy = _tileSize / style.pattern->heightScale;
        }

        std::size_t offset = _coords.size();
        for (int i = 0; i < vertexCount; i++) {
            cglib::vec2<float> p(static_cast<float>(coords[i * 2 + 0]), static_cast<float>(coords[i * 2 + 1]));
            cglib::vec2<float> uv((p(0) + 0.5f) * du_dx, (p(1) + 0.5f) * dv_dy);

            _coords.append(p);
            _texCoords.append(uv);
        }

        cglib::bbox2<float> clipBox(cglib::vec2<float>(0.0f, 0.0f), cglib::vec2<float>(1.0f, 1.0f));
        for (int i = 0; i < elementCount * 3; i += 3) {
            int i0 = elements[i + 0];
            int i1 = elements[i + 1];
            int i2 = elements[i + 2];
            if (i0 == TESS_UNDEF || i1 == TESS_UNDEF || i2 == TESS_UNDEF) {
                continue;
            }

            cglib::bbox2<float> bounds(_coords[i0 + offset]);
            bounds.add(_coords[i1 + offset]);
            bounds.add(_coords[i2 + offset]);
            if (clipBox.inside(bounds)) {
                std::array<std::size_t, 3> srcIndices = { { i0 + offset, i2 + offset, i1 + offset } };
                _transformer->tesselateTriangles(srcIndices.data(), srcIndices.size(), _coords, _texCoords, _indices);
            }
        }

        _attribs.fill(cglib::vec4<std::int8_t>(styleIndex, 0, 0, 0), _coords.size() - offset);

        return true;
    }

    bool TileLayerBuilder::tesselatePolygon3D(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float minHeight, float maxHeight, std::int8_t styleIndex, const Polygon3DStyle& style) {
        cglib::bbox2<float> clipBox(cglib::vec2<float>(0.0f, 0.0f), cglib::vec2<float>(1.0f, 1.0f));
        if (minHeight != maxHeight) {
            for (const std::vector<cglib::vec2<float>>& points : pointsList) {
                std::size_t j = points.size() - 1;
                for (std::size_t i = 0; i < points.size(); i++) {
                    cglib::bbox2<float> bounds(points[i]);
                    bounds.add(points[j]);
                    if (clipBox.inside(bounds)) {
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

        if (!_tessPoolAllocator) {
            _tessPoolAllocator = std::unique_ptr<PoolAllocator>(new PoolAllocator);
        }
        _tessPoolAllocator->reset(); // reuse last allocated block from the start

        TESSalloc ma;
        memset(&ma, 0, sizeof(ma));
        ma.memalloc = [](void* userData, unsigned int size) { return reinterpret_cast<PoolAllocator*>(userData)->allocate(size); };
        ma.memfree = [](void* userData, void* ptr) {};
        ma.userData = _tessPoolAllocator.get();
        ma.extraVertices = 256; // realloc not provided, allow 256 extra vertices.

        TESStesselator* tessPtr = tessNewTess(&ma);
        if (!tessPtr) {
            return false;
        }
        std::shared_ptr<TESStesselator> tess(tessPtr, tessDeleteTess);
        
        for (const std::vector<cglib::vec2<float>>& points : pointsList) {
            TESSreal* coords = reinterpret_cast<TESSreal*>(_tessPoolAllocator->allocate(points.size() * 2 * sizeof(TESSreal)));
            for (std::size_t i = 0; i < points.size(); i++) {
                coords[i * 2 + 0] = static_cast<TESSreal>(points[i](0));
                coords[i * 2 + 1] = static_cast<TESSreal>(points[i](1));
            }
            tessAddContour(tess.get(), 2, coords, 2 * sizeof(TESSreal), static_cast<int>(points.size()));
        }
        tessTesselate(tess.get(), TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, 0);
        const TESSreal* coords = tessGetVertices(tess.get());
        const int* elements = tessGetElements(tess.get());
        int vertexCount = tessGetVertexCount(tess.get());
        int elementCount = tessGetElementCount(tess.get());

        std::size_t offset = _coords.size();
        for (int i = 0; i < vertexCount; i++) {
            cglib::vec2<float> p(static_cast<float>(coords[i * 2 + 0]), static_cast<float>(coords[i * 2 + 1]));

            _coords.append(p);
            _texCoords.append(p);
        }

        for (int i = 0; i < elementCount * 3; i += 3) {
            int i0 = elements[i + 0];
            int i1 = elements[i + 1];
            int i2 = elements[i + 2];
            if (i0 == TESS_UNDEF || i1 == TESS_UNDEF || i2 == TESS_UNDEF) {
                continue;
            }

            cglib::bbox2<float> bounds(_coords[i0 + offset]);
            bounds.add(_coords[i1 + offset]);
            bounds.add(_coords[i2 + offset]);
            if (clipBox.inside(bounds)) {
                std::array<std::size_t, 3> srcIndices = { { i0 + offset, i2 + offset, i1 + offset } };
                _transformer->tesselateTriangles(srcIndices.data(), srcIndices.size(), _coords, _texCoords, _indices);
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
        float minMiterDot = stroke ? 1.0f : MIN_MITER_DOT;
        float linePos = 0;

        std::size_t i = 1;
        for (; i < points.size(); i++) {
            if (points[i] != points[i - 1]) {
                break;
            }
        }
        if (i == points.size()) {
            return false;
        }

        cglib::vec2<float> knotBinormal(0, 0);
        if (cycle) {
            std::size_t j = points.size() - 1;
            for (; j > 0; j--) {
                if (points[j] != points[j - 1]) {
                    break;
                }
            }
            if (j == 0) {
                return false;
            }

            cglib::vec2<float> prevTangent(cglib::unit(points[j] - points[j - 1]));
            cglib::vec2<float> prevBinormal = cglib::vec2<float>(prevTangent(1), -prevTangent(0));
            cglib::vec2<float> tangent(cglib::unit(points[i] - points[i - 1]));
            cglib::vec2<float> binormal = cglib::vec2<float>(tangent(1), -tangent(0));

            float dot = cglib::dot_product(binormal, prevBinormal);
            if (dot < minMiterDot) {
                cycle = endpoints = false;
            }
            else {
                knotBinormal = cglib::unit(binormal + prevBinormal) * (1 / std::sqrt((1 + dot) / 2));
            }
        }

        cglib::vec2<float> binormal(0, 0), tangent(0, 0);
        {
            const cglib::vec2<float>& p0 = points[i - 1];
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
            _binormals.append(cycle ? -knotBinormal : -binormal, cycle ? knotBinormal : binormal);
            _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 1), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 1));
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
            if (dot < minMiterDot) {
                _coords.append(p0, p0);
                _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
                _binormals.append(-prevBinormal, prevBinormal);
                _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 1), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 1));

                _coords.append(p0, p0);
                _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
                _binormals.append(-binormal, binormal);
                _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 1), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 1));
            }
            else {
                cglib::vec2<float> lerpedBinormal = cglib::unit(binormal + prevBinormal) * (1 / std::sqrt((1 + dot) / 2));

                _coords.append(p0, p0);
                _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
                _binormals.append(-lerpedBinormal, lerpedBinormal);
                _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 1), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 1));
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
            _binormals.append(cycle ? -knotBinormal : -binormal, cycle ? knotBinormal : binormal);
            _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 0, 1, 1), cglib::vec4<std::int8_t>(styleIndex, 0, -1, 1));

            if (endpoints) {
                std::size_t i1 = _coords.size();
                tesselateLineEndPoint(p0, u0, v0, v1, i1, i0, tangent, binormal, styleIndex, style);
            }
        }
        return true;
    }

    bool TileLayerBuilder::tesselateLineEndPoint(const cglib::vec2<float>& p0, float u0, float v0, float v1, std::size_t i0, std::size_t i1, const cglib::vec2<float>& tangent, const cglib::vec2<float>& binormal, std::int8_t styleIndex, const LineStyle& style) {
        if (_clipBox.inside(p0)) {
            _coords.append(p0, p0);
            _texCoords.append(cglib::vec2<float>(u0, v0), cglib::vec2<float>(u0, v1));
            _binormals.append(tangent - binormal, tangent + binormal);
            _attribs.append(cglib::vec4<std::int8_t>(styleIndex, 1, 1, 1), cglib::vec4<std::int8_t>(styleIndex, 1, -1, 1));

            _indices.append(i0 + 1, i1 + 0, i0 + 0);
            _indices.append(i0 + 1, i1 + 1, i1 + 0);
        }
        return true;
    }
} }

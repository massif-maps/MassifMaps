/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILELAYERBUILDER_H_
#define _CARTO_VT_TILELAYERBUILDER_H_

#include "TileId.h"
#include "TileBackground.h"
#include "TileBitmap.h"
#include "TileGeometry.h"
#include "TileLabel.h"
#include "TileLayer.h"
#include "TileTransformer.h"
#include "Styles.h"
#include "PolygonTesselator.h"
#include "VertexArray.h"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>
#include <list>
#include <functional>

#include <cglib/vec.h>
#include <cglib/bbox.h>

namespace carto { namespace vt {
    class TileLayerBuilder final {
    public:
        using Vertex = cglib::vec2<float>;
        using Vertices = std::vector<Vertex>;
        using VerticesList = std::vector<Vertices>;

        using PointProcessor = std::function<void(long long id, const Vertex& vertex)>;
        using TextProcessor = std::function<void(long long id, const Vertex& vertex, const std::string& text)>;
        using LineProcessor = std::function<void(long long id, const Vertices& vertices)>;
        using PolygonProcessor = std::function<void(long long id, const VerticesList& verticesList)>;
        using Polygon3DProcessor = std::function<void(long long id, const VerticesList& verticesList, float minHeight, float maxHeight)>;
        using PointLabelProcessor = std::function<void(long long localId, long long globalId, long long groupId, const std::variant<Vertex, Vertices>& position, float priority, float minimumGroupDistance)>;
        using TextLabelProcessor = std::function<void(long long localId, long long globalId, long long groupId, const std::optional<Vertex>& position, const Vertices& vertices, const std::string& text, float priority, float minimumGroupDistance)>;

        explicit TileLayerBuilder(const TileId& tileId, int layerIdx, std::shared_ptr<const TileTransformer::VertexTransformer> transformer, float tileSize, float geomScale);

        void setClipBox(const cglib::bbox2<float>& clipBox);

        void addBackground(const std::shared_ptr<TileBackground>& background);
        void addBitmap(const std::shared_ptr<TileBitmap>& bitmap);

        PointProcessor createPointProcessor(const PointStyle& style, const std::shared_ptr<GlyphMap>& glyphMap);
        TextProcessor createTextProcessor(const TextStyle& style, const TextFormatter& formatter);
        LineProcessor createLineProcessor(const LineStyle& style, const std::shared_ptr<StrokeMap>& strokeMap);
        PolygonProcessor createPolygonProcessor(const PolygonStyle& style);
        Polygon3DProcessor createPolygon3DProcessor(const Polygon3DStyle& style);
        PointLabelProcessor createPointLabelProcessor(const PointLabelStyle& style, const std::shared_ptr<GlyphMap>& glyphMap);
        TextLabelProcessor createTextLabelProcessor(const TextLabelStyle& style, const TextFormatter& formatter);

        std::shared_ptr<TileLayer> buildTileLayer(std::optional<CompOp> compOp, FloatFunction opacityFunc) const;

    private:
        static constexpr unsigned int RESERVED_VERTICES = 4096;

        static constexpr float MIN_MITER_DOT = -0.8f; // minimum allowed dot product result between segment direction vectors, if less, then miter-join is not used
        static constexpr float STROKE_MIN_MITER_DOT = 0.2f; // minimum allowed dot product result between segment direction vectors for stroked lines, if less, then miter-join is not used

        struct BuilderParameters {
            TileGeometry::Type type;
            int parameterCount;
            std::array<ColorFunction, TileGeometry::StyleParameters::MAX_PARAMETERS> colorFuncs;
            std::array<FloatFunction, TileGeometry::StyleParameters::MAX_PARAMETERS> widthFuncs;
            std::array<FloatFunction, TileGeometry::StyleParameters::MAX_PARAMETERS> strokeWidthFuncs;
            std::array<StrokeMap::StrokeId, TileGeometry::StyleParameters::MAX_PARAMETERS> lineStrokeIds;
            std::shared_ptr<const StrokeMap> strokeMap;
            std::shared_ptr<const GlyphMap> glyphMap;
            std::shared_ptr<const BitmapPattern> pattern;
            std::optional<Transform> transform;
            CompOp compOp;

            BuilderParameters() : type(TileGeometry::Type::NONE), parameterCount(0), colorFuncs(), widthFuncs(), strokeWidthFuncs(), lineStrokeIds(), strokeMap(), glyphMap(), pattern(), transform(), compOp(CompOp::SRC_OVER) { }
        };

        void appendGeometry();
        void packGeometry(std::vector<std::shared_ptr<TileGeometry>>& geometryList) const;
        void packGeometry(TileGeometry::Type type, int dimensions, float coordScale, float binormalScale, float texCoordScale, float heightScale, const VertexArray<cglib::vec3<float>>& coords, const VertexArray<cglib::vec2<float>>& texCoords, const VertexArray<cglib::vec3<float>>& normals, const VertexArray<cglib::vec3<float>>& binormals, const VertexArray<float>& heights, const VertexArray<cglib::vec4<std::int8_t>>& attribs, const VertexArray<std::size_t>& indices, const VertexArray<long long>& ids, const TileGeometry::StyleParameters& styleParameters, std::vector<std::shared_ptr<TileGeometry>>& geometryList) const;

        bool tesselateGlyph(const cglib::vec2<float>& point, std::int8_t styleIndex, const cglib::vec2<float>& pen, const cglib::vec2<float>& size, const GlyphMap::Glyph* glyph);
        bool tesselatePolygon(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, std::int8_t styleIndex, const PolygonStyle& style);
        bool tesselatePolygon3D(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float minHeight, float maxHeight, std::int8_t styleIndex, const Polygon3DStyle& style);
        bool tesselateLine(const std::vector<cglib::vec2<float>>& points, std::int8_t styleIndex, const StrokeMap::Stroke* stroke, const LineStyle& style);
        bool tesselateLineEndPoint(const cglib::vec2<float>& p0, float u0, float v0, float v1, std::size_t i0, std::size_t i1, const cglib::vec2<float>& tangent, const cglib::vec2<float>& binormal, std::int8_t styleIndex, const LineStyle& style);

        const TileId _tileId;
        const int _layerIdx;
        const float _tileSize;
        const float _geomScale;
        const std::shared_ptr<const TileTransformer::VertexTransformer> _transformer;
        cglib::bbox2<float> _clipBox;
        cglib::bbox2<float> _polygonClipBox;

        BuilderParameters _builderParameters;
        std::shared_ptr<const TileLabel::Style> _labelStyle;
        PolygonTesselator _tesselator;

        VertexArray<cglib::vec2<float>> _coords;
        VertexArray<cglib::vec2<float>> _texCoords;
        VertexArray<cglib::vec2<float>> _binormals;
        VertexArray<float> _heights;
        VertexArray<cglib::vec4<std::int8_t>> _attribs;
        VertexArray<std::size_t> _indices;
        VertexArray<long long> _ids;

        std::vector<std::shared_ptr<TileBackground>> _backgroundList;
        std::vector<std::shared_ptr<TileBitmap>> _bitmapList;
        std::vector<std::shared_ptr<TileGeometry>> _geometryList;
        std::vector<std::shared_ptr<TileLabel>> _labelList;
    };
} }

#endif

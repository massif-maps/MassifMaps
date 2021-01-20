/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILELAYERBUILDER_H_
#define _CARTO_VT_TILELAYERBUILDER_H_

#include "TileId.h"
#include "TileBitmap.h"
#include "TileGeometry.h"
#include "TileLabel.h"
#include "TileLayer.h"
#include "TileTransformer.h"
#include "Styles.h"
#include "PoolAllocator.h"
#include "VertexArray.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <list>
#include <functional>

#include <boost/variant.hpp>

#include <cglib/vec.h>
#include <cglib/bbox.h>

namespace carto { namespace vt {
    class TileLayerBuilder final {
    public:
        using Vertex = cglib::vec2<float>;
        using Vertices = std::vector<Vertex>;
        using VerticesList = std::vector<Vertices>;

        struct PointLabelInfo {
            long long id = 0;
            long long groupId = 0;
            boost::variant<Vertex, Vertices> position;
            float priority = 0;
            float minimumGroupDistance = 0;

            PointLabelInfo() = default;
            explicit PointLabelInfo(long long id, long long groupId, boost::variant<Vertex, Vertices> position, float priority, float minimumGroupDistance) : id(id), groupId(groupId), position(std::move(position)), priority(priority), minimumGroupDistance(minimumGroupDistance) { }
        };

        struct TextLabelInfo {
            long long id = 0;
            long long groupId = 0;
            std::string text;
            boost::optional<Vertex> position;
            Vertices vertices;
            float priority = 0;
            float minimumGroupDistance = 0;

            TextLabelInfo() = default;
            explicit TextLabelInfo(long long id, long long groupId, std::string text, boost::optional<Vertex> position, Vertices vertices, float priority, float minimumGroupDistance) : id(id), groupId(groupId), text(std::move(text)), position(std::move(position)), vertices(std::move(vertices)), priority(priority), minimumGroupDistance(minimumGroupDistance) { }
        };

        explicit TileLayerBuilder(const TileId& tileId, int layerIdx, std::shared_ptr<const TileTransformer::VertexTransformer> transformer, float tileSize, float geomScale);

        void setClipBox(const cglib::bbox2<float>& clipBox);

        void addBitmap(const std::shared_ptr<TileBitmap>& bitmap);
        void addPoints(const std::function<bool(long long& id, Vertex& vertex)>& generator, const PointStyle& style, const std::shared_ptr<GlyphMap>& glyphMap);
        void addTexts(const std::function<bool(long long& id, Vertex& vertex, std::string& text)>& generator, const TextStyle& style, const TextFormatter& formatter);
        void addLines(const std::function<bool(long long& id, Vertices& vertices)>& generator, const LineStyle& style, const std::shared_ptr<StrokeMap>& strokeMap);
        void addPolygons(const std::function<bool(long long& id, VerticesList& verticesList)>& generator, const PolygonStyle& style);
        void addPolygons3D(const std::function<bool(long long& id, VerticesList& verticesList)>& generator, float minHeight, float maxHeight, const Polygon3DStyle& style);
        void addPointLabels(const std::function<bool(long long& id, PointLabelInfo& labelInfo)>& generator, const PointLabelStyle& style, const std::shared_ptr<GlyphMap>& glyphMap);
        void addTextLabels(const std::function<bool(long long& id, TextLabelInfo& labelInfo)>& generator, const TextLabelStyle& style, const TextFormatter& formatter);

        std::shared_ptr<TileLayer> buildTileLayer(boost::optional<CompOp> compOp, FloatFunction opacityFunc) const;

    private:
        constexpr static unsigned int RESERVED_VERTICES = 4096;

        constexpr static float MIN_MITER_DOT = -0.8f; // minimum allowed dot product result between segment direction vectors, if less, then miter-join is not used
        constexpr static float STROKE_MIN_MITER_DOT = 0.2f; // minimum allowed dot product result between segment direction vectors for stroked lines, if less, then miter-join is not used

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
            boost::optional<cglib::mat3x3<float>> transform;
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
        
        BuilderParameters _builderParameters;
        std::shared_ptr<const TileLabel::Style> _labelStyle;

        VertexArray<cglib::vec2<float>> _coords;
        VertexArray<cglib::vec2<float>> _texCoords;
        VertexArray<cglib::vec2<float>> _binormals;
        VertexArray<float> _heights;
        VertexArray<cglib::vec4<std::int8_t>> _attribs;
        VertexArray<std::size_t> _indices;
        VertexArray<long long> _ids;

        std::vector<std::shared_ptr<TileBitmap>> _bitmapList;
        std::vector<std::shared_ptr<TileGeometry>> _geometryList;
        std::vector<std::shared_ptr<TileLabel>> _labelList;

        std::unique_ptr<PoolAllocator> _tessPoolAllocator;
    };
} }

#endif

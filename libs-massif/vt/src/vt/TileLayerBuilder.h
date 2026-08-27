/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_TILELAYERBUILDER_H_
#define _MASSIF_VT_TILELAYERBUILDER_H_

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
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <list>
#include <functional>

#include <cglib/vec.h>
#include <cglib/bbox.h>

namespace massif::vt {
    class TileLayerBuilder final {
    public:
        using Vertex = cglib::vec2<float>;
        using Vertices = std::vector<Vertex>;
        using VerticesList = std::vector<Vertices>;

        // The value the selecting style parameter holds, shared with the renderer: it decides which
        // of a variant's two slots each feature takes, and a change to it is answered by rewriting
        // style bytes rather than by decoding the tile again.
        void setStyleState(StyleStateRef styleState, std::uint64_t stateKey) { _styleState = std::move(styleState); _stateKey = stateKey; }

        // A STYLE VARIANT is one feature drawn once but carrying both of the styles a selecting
        // style parameter can give it. The decoder folds the comparison both ways and registers the
        // two styles between these calls; the vertices tesselated in between are recorded with both
        // slots and with stateKey, the hash of the field value the parameter is compared with.
        //
        // Exactly two styles must be registered, and only the ACTIVE one is drawn - the other is
        // registered by running its processor over an empty feature collection, or, when that
        // branch paints nothing at all, by reserveInvisibleLineStyle.
        void beginStyleVariant(std::uint64_t stateKey);
        void reserveInvisibleLineStyle();
        void endStyleVariant(int selectedSlot, bool selected);

        using PointProcessor = std::function<void(long long id, const Vertex& vertex)>;
        using TextProcessor = std::function<void(long long id, const Vertex& vertex, const std::string& text, int geoPointIndex)>;
        using LineProcessor = std::function<void(long long id, const Vertices& vertices)>;
        using PolygonProcessor = std::function<void(long long id, const VerticesList& verticesList)>;
        using Polygon3DProcessor = std::function<void(long long id, const VerticesList& verticesList, float minHeight, float maxHeight)>;
        using PointLabelProcessor = std::function<void(long long id, long long labelId, long long groupId, const std::variant<Vertex, Vertices>& position, float priority, float minimumGroupDistance, bool allowOverlapSameFeatureId, bool sameFeatureIdDependent, int geoPointIndex)>;
        using TextLabelProcessor = std::function<void(long long id, long long labelId, long long groupId, const std::optional<Vertex>& position, const Vertices& vertices, const std::string& text, float priority, float minimumGroupDistance, bool allowOverlapSameFeatureId, bool sameFeatureIdDependent, int geoPointIndex)>;

        explicit TileLayerBuilder(std::string layerName, int layerIdx, const TileId& tileId, const std::shared_ptr<const TileTransformer>& transformer, float tileSize, float geomScale);

        void setCompOp(std::optional<CompOp> compOp);
        void setOpacityFunc(FloatFunction opacityFunc);
        void setClipBox(const cglib::bbox2<float>& clipBox);
        // Height in metres at which an extrusion wall gets an extra ring. The 3D lighting is
        // evaluated PER VERTEX, so the facade gradient is a straight line between a wall's base
        // and its roof unless there is a vertex where the gradient's own curve knees - which is
        // what makes 'building-vertical-gradient-height' mean anything on a wall taller than it.
        // 0 = no split.
        void setPolygon3DGradientHeight(float height) { _polygon3DGradientHeight = height; }
        // Metres the contact shadow on the ground reaches out from a footprint. 0 = no skirt.
        void setPolygon3DGroundRadius(float radius) { _polygon3DGroundRadius = radius; }
        // Metres between subdivisions along a wall of the contact shadow. 0 = the terrain grid cell.
        void setPolygon3DGroundStep(float step) { _polygon3DGroundStep = step; }
        // Metres of bevel between a wall and the roof, rounding the edge. 0 = a hard 90 degrees.
        void setPolygon3DEdgeRadius(float radius) { _polygon3DEdgeRadius = radius; }
        // True blends the bevel's normal from wall to roof, so the edge reads as ROLLED. False
        // holds it halfway across the band, making it a facet with a tone of its own - a rim
        // tracing every roof, which is what separates one building from the next looking straight
        // down. mapbox's fill-extrusion-rounded-roof.
        void setPolygon3DRoundedRoof(bool rounded) { _polygon3DRoundedRoof = rounded; }
        void setPolygonClipBox(const cglib::bbox2<float>& clipBox);

        void addBackground(const std::shared_ptr<TileBackground>& background);
        void addBitmap(const std::shared_ptr<TileBitmap>& bitmap);

        PointProcessor createPointProcessor(const PointStyle& style, const std::shared_ptr<GlyphMap>& glyphMap);
        TextProcessor createTextProcessor(const TextStyle& style, const TextFormatter& formatter);
        LineProcessor createLineProcessor(const LineStyle& style, const std::shared_ptr<StrokeMap>& strokeMap);
        PolygonProcessor createPolygonProcessor(const PolygonStyle& style);
        Polygon3DProcessor createPolygon3DProcessor(const Polygon3DStyle& style);
        PointLabelProcessor createPointLabelProcessor(const PointLabelStyle& style, const std::shared_ptr<GlyphMap>& glyphMap);
        TextLabelProcessor createTextLabelProcessor(const TextLabelStyle& style, const TextFormatter& formatter);

        std::shared_ptr<TileLayer> buildTileLayer() const;

    private:
        static constexpr unsigned int RESERVED_VERTICES = 4096;

        struct BuilderParameters {
            TileGeometry::Type type;
            int parameterCount;
            std::array<ColorFunction, TileGeometry::StyleParameters::MAX_PARAMETERS> colorFuncs;
            std::array<FloatFunction, TileGeometry::StyleParameters::MAX_PARAMETERS> widthFuncs;
            std::array<FloatFunction, TileGeometry::StyleParameters::MAX_PARAMETERS> offsetFuncs;
            std::array<FloatFunction, TileGeometry::StyleParameters::MAX_PARAMETERS> gapWidthFuncs;
            std::array<FloatFunction, TileGeometry::StyleParameters::MAX_PARAMETERS> blurFuncs;
            std::array<StrokeMap::StrokeId, TileGeometry::StyleParameters::MAX_PARAMETERS> lineStrokeIds;
            std::array<bool, TileGeometry::StyleParameters::MAX_PARAMETERS> patternUsed; // this slot is a patterned fill
            std::shared_ptr<const StrokeMap> strokeMap;
            std::shared_ptr<const GlyphMap> glyphMap;
            std::shared_ptr<const BitmapPattern> pattern;
            cglib::vec2<float> translate;
            CompOp compOp;
            int glyphRenderSize;

            // patternUsed defaults to TRUE so every path that does not manage it (lines, points)
            // keeps sampling its pattern; only the polygon processor sets it per slot.
            BuilderParameters() : type(TileGeometry::Type::NONE), parameterCount(0), colorFuncs(), widthFuncs(), offsetFuncs(), gapWidthFuncs(), blurFuncs(), lineStrokeIds(), patternUsed(), strokeMap(), glyphMap(), pattern(), translate(0, 0), compOp(CompOp::SRC_OVER), glyphRenderSize(64) { patternUsed.fill(true); }
        };

        void packGeometry(std::vector<std::shared_ptr<TileGeometry>>& geometryList) const;
        // The skirt stream, packed once per layer into its own POLYGON3DGROUND geometry.
        void packGroundSkirt(std::vector<std::shared_ptr<TileGeometry>>& geometryList) const;
        void packGeometry(TileGeometry::Type type, int dimensions, float coordScale, float binormalScale, float texCoordScale, float heightScale, const VertexArray<cglib::vec3<float>>& coords, const VertexArray<cglib::vec2<float>>& texCoords, const VertexArray<cglib::vec3<float>>& normals, const VertexArray<cglib::vec3<float>>& binormals, const VertexArray<float>& heights, const VertexArray<cglib::vec4<std::int8_t>>& attribs, const VertexArray<std::size_t>& indices, const VertexArray<long long>& ids, const VertexArray<std::uint16_t >& geoPosIndexes, const TileGeometry::StyleParameters& styleParameters, std::vector<TileGeometry::FeatureStyleRange> featureStyleRanges, std::vector<std::shared_ptr<TileGeometry>>& geometryList) const;
        void registerStyleVariantSlot(int styleIndex);

        bool tesselateGlyph(const cglib::vec2<float>& point, std::int8_t styleIndex, const cglib::vec2<float>& pen, const cglib::vec2<float>& size, const GlyphMap::Glyph* glyph);
        bool tesselatePolygon(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, std::int8_t styleIndex, const PolygonStyle& style);
        bool tesselatePolygon3D(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float minHeight, float maxHeight, std::int8_t styleIndex, const Polygon3DStyle& style);
        // The footprint pulled inward, for the roof ring an edge radius leaves behind. Returns the
        // inset ACTUALLY used in tile-local units - clamped to what the narrowest edge can give up,
        // and 0 when the ring cannot take any, in which case the extrusion keeps its hard edge.
        float insetRings(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float radius, std::vector<std::vector<cglib::vec2<float>>>& insetList) const;
        // The band bridging the top of a wall to the inset roof ring. Mitred at the corners: two
        // adjacent bands share the edge from the footprint corner to its inset roof vertex.
        void appendBevelQuad(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1, const cglib::vec2<float>& i0p, const cglib::vec2<float>& i1p, const cglib::vec2<float>& binormal, float wallTop, float roofHeight, std::int8_t styleIndex);
        // One wall quad of an extrusion, over the height range [lo, hi].
        void appendWallQuad(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1, const cglib::vec2<float>& binormal, float lo, float hi, std::int8_t styleIndex);
        // The contact shadow on the ground around one footprint ring: one quad per edge, covering
        // that edge's bounding capsule. The fragment measures its own distance to the segment, so
        // corners are round and the overlaps are resolved by MIN blending rather than avoided here.
        // Accumulated apart from the walls because the builder has ONE vertex stream, keyed by
        // _builderParameters.type, and this is a different geometry.
        void appendGroundSkirt(const std::vector<cglib::vec2<float>>& points, float height, bool hole, std::int8_t styleIndex);

        // A shaped roof on top of the walls, from the OSM roof:shape tag. Returns false when the
        // footprint cannot carry one, in which case the caller tesselates a flat roof as before.
        bool appendRoof(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float baseHeight, float roofHeight, RoofShape shape, std::int8_t styleIndex);
        // A footprint plus the height its roof sits at. Order- and winding-independent (the parts
        // are summed), so the same building digitised twice from a different start vertex matches.
        static std::uint64_t roofKey(const std::vector<std::vector<cglib::vec2<float>>>& pointsList, float height);
        // An undirected footprint edge, quantised to a fine tile grid so two features that share
        // a wall land on the same key.
        std::uint64_t wallEdgeKey(const cglib::vec2<float>& p0, const cglib::vec2<float>& p1) const;
        bool tesselateLine(const std::vector<cglib::vec2<float>>& points, std::int8_t styleIndex, const StrokeMap::Stroke* stroke, const LineStyle& style);
        bool tesselateLineEndPoint(const cglib::vec2<float>& p0, float u0, float v0, float v1, std::size_t i0, std::size_t i1, const cglib::vec2<float>& tangent, const cglib::vec2<float>& binormal, std::int8_t styleIndex, const LineStyle& style);
        static float lineEndArrowInradius(const LineStyle& style);
        static std::vector<cglib::vec2<float>> lineEndArrowShapeOutline(const LineStyle& style);
        bool tesselateLineEndArrowShape(const cglib::vec2<float>& p0, float u0, float v0, float v1, const cglib::vec2<float>& tangent, const cglib::vec2<float>& binormal, std::int8_t styleIndex, const LineStyle& style);
        bool tesselateLineEndArrow(const cglib::vec2<float>& p0, float u0, float v0, float v1, const cglib::vec2<float>& tangent, const cglib::vec2<float>& binormal, std::int8_t styleIndex, const LineStyle& style);
        void appendGeometry();

        const std::string _layerName;
        const int _layerIdx;
        const TileId _tileId;
        const std::shared_ptr<const TileTransformer::VertexTransformer> _transformer;
        const float _tileSize;
        const float _geomScale;

        std::optional<CompOp> _compOp;
        FloatFunction _opacityFunc = FloatFunction(1.0f);
        cglib::bbox2<float> _clipBox = cglib::bbox2<float>(cglib::vec2<float>(-0.125f, -0.125f), cglib::vec2<float>(1.125f, 1.125f));
        cglib::bbox2<float> _polygonClipBox = cglib::bbox2<float>(cglib::vec2<float>(-0.001953125f, -0.001953125), cglib::vec2<float>(1.001953125f, 1.001953125f));

        BuilderParameters _builderParameters;
        std::shared_ptr<const TileLabel::Style> _labelStyle;
        PolygonTesselator _tesselator;

        VertexArray<cglib::vec2<float>> _coords;
        VertexArray<cglib::vec2<float>> _texCoords;
        VertexArray<cglib::vec2<float>> _binormals;
        VertexArray<float> _heights;
        VertexArray<cglib::vec4<std::int8_t>> _attribs;
        // Height range already walled on each footprint edge of this layer. An OSM building and its
        // building:part share edges, and two coincident walls z-fight into a stipple that reads as
        // shadow acne; the second one is emitted only where the first left a gap.
        std::unordered_map<std::uint64_t, std::pair<float, float>> _polygon3DWalls;
        // The current extrusion's footprint centroid, carried by every one of its vertices.
        cglib::vec2<float> _polygon3DCentroid = cglib::vec2<float>(0, 0);
        // ...and how far its footprint reaches from that centroid, in 1/512 of a tile.
        std::int8_t _polygon3DExtent = 0;
        float _polygon3DGradientHeight = 0.0f;
        // Roofs already emitted this layer. Walls are deduped per edge above; a roof is a polygon,
        // so it is matched whole - which catches a duplicated footprint, the case that actually
        // z-fights. A building:part normally differs in height from its parent, putting its roof on
        // another plane entirely.
        std::unordered_set<std::uint64_t> _polygon3DRoofs;
        float _polygon3DGroundRadius = 0.0f;   // metres the contact shadow reaches; 0 = off
        float _polygon3DGroundStep = 0.0f;     // metres between subdivisions; 0 = the terrain grid cell
        float _polygon3DEdgeRadius = 0.0f;     // metres of bevel at the roof edge; 0 = hard edge
        bool _polygon3DRoundedRoof = true;     // false = the bevel is a flat facet with its own tone
        // The skirt's own stream (see appendGroundSkirt), packed once by buildTileLayer.
        VertexArray<cglib::vec2<float>> _groundCoords;
        // (along, across, length) of the footprint segment this vertex belongs to, in units of the
        // shadow radius. The fragment measures its own distance from it.
        VertexArray<cglib::vec3<float>> _groundBinormals;
        // Tile-local, so the ground shader can discard what belongs to a neighbouring tile.
        VertexArray<cglib::vec2<float>> _groundTexCoords;
        VertexArray<float> _groundHeights;
        VertexArray<cglib::vec4<std::int8_t>> _groundAttribs;
        VertexArray<std::size_t> _groundIndices;
        VertexArray<long long> _groundIds;
        VertexArray<std::size_t> _indices;
        VertexArray<std::uint16_t> _geoPosIndexes;
        VertexArray<long long> _ids;
        // A style variant while it is open, and the vertex runs the closed ones left behind. The
        // runs are in _coords space here; packGeometry maps them onto the repacked vertices.
        struct StyleVariantRange {
            std::size_t firstVertex;
            std::size_t vertexCount;
            std::uint64_t stateKey;
            std::uint8_t styleIndices[2];
        };

        StyleStateRef _styleState;
        std::uint64_t _stateKey = 0;
        std::vector<StyleVariantRange> _styleVariantRanges;
        std::vector<int> _styleVariantSlots; // the slots registered while a variant is open
        std::size_t _styleVariantFirstVertex = 0;
        std::uint64_t _styleVariantStateKey = 0;
        int _styleVariantGeneration = -1; // the geometry the variant opened in; -1 = none open
        int _geometryGeneration = 0;

        std::vector<std::shared_ptr<TileBackground>> _backgroundList;
        std::vector<std::shared_ptr<TileBitmap>> _bitmapList;
        std::vector<std::shared_ptr<TileGeometry>> _geometryList;
        std::vector<std::shared_ptr<TileLabel>> _labelList;
    };
}

#endif

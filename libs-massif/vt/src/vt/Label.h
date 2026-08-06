/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_LABEL_H_
#define _CARTO_VT_LABEL_H_

#include "TileId.h"
#include "TileLabel.h"

#include "TileTransformer.h"
#include "Color.h"
#include "Bitmap.h"
#include "Font.h"
#include "ViewState.h"
#include "VertexArray.h"
#include "Styles.h"

#include <cstdint>
#include <memory>
#include <array>
#include <list>
#include <vector>
#include <limits>
#include <algorithm>

namespace carto::vt {
    class Label final {
    public:
        explicit Label(const TileLabel& tileLabel, const TileId& tileId, int layerIdx, const cglib::mat4x4<double>& tileMatrix, const std::shared_ptr<const TileTransformer::VertexTransformer>& transformer);

        long long getGlobalId() const { return _globalId; }
        long long getLocalId() const { return _placement ? _placement->localId : _localId; }
        int getGeoPointIndex() const { return _geoPointIndex; }
        long long getGroupId() const { return _groupId; }

        TileId getTileId() const { return _placement ? _placement->tileId : _tileId; }
        int getLayerIndex() const { return _layerIndex; }

        const std::shared_ptr<const TileLabel::Style>& getStyle() const { return _style; }

        cglib::vec3<float> getNormal() const { return _placement ? _placement->normal : cglib::vec3<float>(0, 0, 0); }
        float getPriority() const { return _priority; }
        float getMinimumGroupDistance() const { return _minimumGroupDistance; }
        bool allowOverlapSameFeatureId() const { return _allowOverlapSameFeatureId; }
        bool sameFeatureIdDependent() const { return _sameFeatureIdDependent; }

        bool isValid() const { return (bool) _placement; }

        float getOpacity() const { return _opacity; }
        void setOpacity(float opacity) { _opacity = opacity; }

        bool isVisible() const { return _visible; }
        void setVisible(bool visible) { _visible = visible; }

        bool isActive() const { return _active; }
        void setActive(bool active) { _active = active; }

        // Identifies the set of tile geometries this label was built from, so that a rebuild
        // can tell whether anything about its source actually changed (see
        // GLTileRenderer::buildLabelMaps). Order-independent: the merge order follows the
        // visible tile order, which is not stable.
        void setGeometrySignature(long long hash, int count) { _geometryHash = hash; _geometryCount = count; }
        bool hasGeometrySignature(long long hash, int count) const { return _geometryCount == count && _geometryHash == hash && count > 0; }

        // Terrain re-anchoring state (see updateElevation). A label is anchored once, when it
        // is built, and re-anchored only when the elevation under one of the tiles it is built
        // from actually changes. Re-anchoring costs one elevation sample per line vertex, so
        // doing it for every label on every tile-set change is a whole-screen resample.
        bool isElevationDirty() const { return _elevationDirty; }
        void setElevationDirty(bool dirty) { _elevationDirty = dirty; }
        bool hasGeometryOverTile(const TileId& tileId) const;

        void mergeGeometries(Label& label);
        void snapPlacement(const Label& label);
        bool updatePlacement(const ViewState& viewState);
        void updateElevation(const std::function<double(const cglib::vec3<double>&)>& heightFunc);

        bool calculateCenter(cglib::vec3<double>& pos) const;
        bool calculateEnvelope(const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const { return calculateEnvelope((_style->sizeFunc)(viewState), 0, viewState, envelope); }
        bool calculateEnvelope(float size, float buffer, const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const;
        bool calculateVertexData(const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const { return calculateVertexData((_style->sizeFunc)(viewState), viewState, styleIndex, haloStyleIndex, vertices, offsets, normals, texCoords, attribs, indices); }
        bool calculateVertexData(float size, const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;

    private:
        // How labelVsh must read a glyph offset (attribs[3]); see calculateVertexData.
        static constexpr std::int8_t WORLD_OFFSET = 0;       // already spanned, add it as is
        static constexpr std::int8_t CAMERA_AXIS_OFFSET = 1; // x/y on the camera axes

        static constexpr unsigned int MAX_LABEL_VERTICES = 16384;
        static constexpr unsigned int MAX_LINE_FITTING_ITERATIONS = 1; // number of iterations for line glyph placement on corners

        static constexpr float SUMMED_ANGLE_SPLIT_THRESHOLD = 2.09f; // maximum sum of segment angles, in radians
        static constexpr float SINGLE_ANGLE_SPLIT_THRESHOLD = 1.57f; // maximum single segment angle, in radians
        static constexpr float MIN_LINE_SEGMENT_DOTPRODUCT = 0.5f; // the minimum allowed dot product between consecutive segments
        static constexpr float MIN_LINE_SEGMENT_DOTPRODUCT_KEEP = 0.3f; // the same, for a run that is already laid out (hysteresis)
        static constexpr float MAX_LINE_RUN_ANGLE_SPREAD = 1.22f; // total turn a glyph run may take, in radians (~70 deg)
        static constexpr float MAX_LINE_RUN_ANGLE_SPREAD_KEEP = 1.57f; // the same, for a run that is already laid out (hysteresis)
        static constexpr int LINE_LAYOUT_FAILURE_GRACE = 4; // layouts a run that is already on screen may fail before it is dropped
        static constexpr float MIN_LINE_GLYPH_SPAN = 0.5f; // shortest span a glyph takes its direction from, in glyph units
        // Tangram's hairpin test (CurvedLabel::updateScreenTransform): two segments within a short
        // window whose directions sum to less than this chord point back at each other - an inner
        // angle under ~120 degrees - and the glyphs pile up on each other there. Their window is 20
        // screen pixels; in glyph units (1 unit = the font size) that is a little over one glyph.
        static constexpr float LINE_HAIRPIN_CHORD = 1.7f;
        static constexpr float LINE_DIRECTION_WINDOW = 1.5f; // glyph units
        static constexpr float LINE_REVERSE_HYSTERESIS = 0.02f; // fraction of the run length
        static constexpr float LINE_VERTICAL_RUN_FRACTION = 0.2f; // |dx| below this fraction of the run counts as vertical
        static constexpr double PLACEMENT_ROOM_FACTOR = 1.25; // room the glyph run is given on the line, relative to its own length
        static constexpr double PLACEMENT_SMOOTH_TEXT_FRACTION = 1.0 / 3.0; // line detail below this fraction of the text length is smoothed away before laying out glyphs
        static constexpr double SNAP_MOVE_EPSILON = 1.0e-9; // internal world units (1 unit ~ 38m); a 1px anchor drift is ~1e-4 at z15
        static constexpr float MIN_BILLBOARD_VIEW_NORMAL_DOTPRODUCT = 0.1f; // the minimum allowed dot product between view vector and surface normal (cos ~78.5deg -> labels valid down to ~tilt 11.5; was 0.49 = calibrated to the old 30deg tilt clamp)

        struct TilePoint {
            TileId tileId;
            long long localId;
            cglib::vec3<double> position;
            cglib::vec3<float> normal;
            cglib::vec3<float> xAxis;
            cglib::vec3<float> yAxis;

            explicit TilePoint(const TileId& tileId, long long localId, const cglib::vec3<double>& pos, const cglib::vec3<float>& norm, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis) : tileId(tileId), localId(localId), position(pos), normal(norm), xAxis(xAxis), yAxis(yAxis) { }

            bool operator == (const TilePoint& other) const {
                return tileId == other.tileId && localId == other.localId;
            }

            bool operator != (const TilePoint& other) const {
                return !(*this == other);
            }
        };

        struct TileLine {
            TileId tileId;
            long long localId;
            std::vector<cglib::vec3<double>> vertices;
            cglib::vec3<float> normal;

            explicit TileLine(const TileId& tileId, long long localId, std::vector<cglib::vec3<double>> vertices, const cglib::vec3<float>& norm) : tileId(tileId), localId(localId), vertices(std::move(vertices)), normal(norm) { }

            bool operator == (const TileLine& other) const {
                return tileId == other.tileId && localId == other.localId;
            }

            bool operator != (const TileLine& other) const {
                return !(*this == other);
            }
        };

        struct Placement {
            struct Edge {
                cglib::vec3<float> position0;
                cglib::vec3<float> position1;
                cglib::vec3<float> binormal0;
                cglib::vec3<float> binormal1;
                cglib::vec3<float> xAxis;
                cglib::vec3<float> yAxis;
            };
            
            TileId tileId;
            long long localId;
            std::vector<Edge> edges;
            std::size_t index;
            std::size_t sourceIndex; // index of the anchor segment in the SOURCE line, edges may be a smoothed copy of it
            cglib::vec3<double> position;
            cglib::vec3<float> normal;
            cglib::vec3<float> xAxis;
            cglib::vec3<float> yAxis;

            explicit Placement(const TileId& tileId, long long localId, const cglib::vec3<double>& pos, const cglib::vec3<float>& norm, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis) : tileId(tileId), localId(localId), edges(), index(0), sourceIndex(0), position(pos), normal(norm), xAxis(xAxis), yAxis(yAxis) { }

            explicit Placement(const TileId& tileId, long long localId, const std::vector<cglib::vec3<double>>& vertices, std::size_t index, std::size_t sourceIndex, const cglib::vec3<double>& pos, const cglib::vec3<float>& norm) : tileId(tileId), localId(localId), edges(), index(index), sourceIndex(sourceIndex), position(pos), normal(norm), xAxis(0, 0, 0), yAxis(0, 0, 0) {
                if (vertices.size() > 1) {
                    edges.resize(vertices.size() - 1);
                    for (std::size_t i = 0; i < edges.size(); i++) {
                        Edge& edge = edges[i];
                        edge.position0 = cglib::vec3<float>::convert(vertices[i] - pos);
                        edge.position1 = cglib::vec3<float>::convert(vertices[i + 1] - pos);
                        edge.xAxis = cglib::unit(edge.position1 - edge.position0);
                        edge.yAxis = cglib::unit(cglib::vector_product(norm, edge.xAxis));
                        edge.binormal0 = edge.yAxis;
                        edge.binormal1 = edge.yAxis;
                        if (i > 0) {
                            cglib::vec3<float> binormal = edges[i - 1].yAxis + edges[i].yAxis;
                            if (cglib::norm(binormal) != 0) {
                                binormal = cglib::unit(binormal);
                                edges[i - 1].binormal1 = edges[i].binormal0 = binormal * (1.0f / cglib::dot_product(edges[i - 1].yAxis, binormal));
                            }
                        }
                    }
                    xAxis = edges[index].xAxis;
                    yAxis = edges[index].yAxis;
                }
            }

            void reverse() {
                if (!edges.empty()) {
                    index = edges.size() - 1 - index;
                    std::reverse(edges.begin(), edges.end());
                    for (Edge& edge : edges) {
                        std::swap(edge.position0, edge.position1);
                        std::swap(edge.binormal0, edge.binormal1);
                        edge.binormal0 = -edge.binormal0;
                        edge.binormal1 = -edge.binormal1;
                        edge.xAxis = -edge.xAxis;
                        edge.yAxis = -edge.yAxis;
                    }
                }
                xAxis = -xAxis;
                yAxis = -yAxis;
            }
        };
        
        float calculateTerrainScaleFactor(const Placement& placement, const ViewState& viewState) const;
        float calculateTerrainScaleFactor(const cglib::vec3<double>& position, const ViewState& viewState) const;
        void setupCoordinateSystem(const ViewState& viewState, const std::shared_ptr<const Placement>& placement, cglib::vec3<float>& origin, cglib::vec3<float>& xAxis, cglib::vec3<float>& yAxis) const;
        void buildPointVertexData(VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;
        void updateLineVertexData(const std::shared_ptr<const Placement>& placement, float scale, const ViewState& viewState, bool rebuildForView) const;
        bool buildLineVertexData(const std::shared_ptr<const Placement>& placement, float scale, const ViewState& viewState, const cglib::mat4x4<double>& mvpMatrix, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;

        cglib::bbox3<double> calculateGeometryBBox(const ViewState& viewState) const;
        static void smoothPlacementLine(const std::vector<cglib::vec3<double>>& vertices, std::size_t index, double minEdgeLength, std::vector<cglib::vec3<double>>& smoothedVertices, std::size_t& smoothedIndex);
        static void clampPlacementAnchor(const std::vector<cglib::vec3<double>>& vertices, double textLength, std::size_t& index, cglib::vec3<double>& position);
        std::shared_ptr<const Placement> buildLinePlacement(const TileLine& tileLine, std::size_t index, const cglib::vec3<double>& position) const;
        std::shared_ptr<const Placement> getPlacement(const ViewState& viewState) const;
        std::shared_ptr<const Placement> findSnappedPointPlacement(const cglib::vec3<double>& position, const std::list<TilePoint>& tilePoints, const Placement* oldPlacement = nullptr) const;
        std::shared_ptr<const Placement> findSnappedLinePlacement(const cglib::vec3<double>& position, const std::list<TileLine>& tileLines, const Placement* oldPlacement = nullptr) const;
        std::shared_ptr<const Placement> findClippedPointPlacement(const ViewState& viewState, const std::list<TilePoint>& tilePoints) const;
        std::shared_ptr<const Placement> findClippedLinePlacement(const ViewState& viewState, const std::list<TileLine>& tileLines) const;

        const TileId _tileId;
        const int _layerIndex;
        const int _geoPointIndex;
        const long long _localId;
        const long long _globalId;
        const long long _groupId;
        const std::vector<Font::Glyph> _glyphs;
        const std::shared_ptr<const TileLabel::Style> _style;
        const float _priority;
        const float _minimumGroupDistance;
        const bool _allowOverlapSameFeatureId;
        const bool _sameFeatureIdDependent;

        cglib::bbox2<float> _glyphBBox;
        float _maxGlyphExtent = 0; // largest distance a glyph reaches from the anchor, style transform included
        std::list<TilePoint> _tilePoints;
        std::list<TileLine> _tileLines;

        mutable bool _geometryBBoxValid = false;
        mutable cglib::bbox3<double> _geometryBBox = cglib::bbox3<double>::smallest();

        // Length of the glyph run in world units at the view the placement was made for: the line
        // detail worth following and the room the run needs both derive from it. Refreshed by
        // updatePlacement, carried over by snapPlacement so a re-created label places the same way.
        double _placementTextLength = 0;

        float _opacity = 0.0f;
        bool _visible = false;
        bool _active = false;
        bool _elevationDirty = true; // built flat: anchor it onto the terrain on the next frame
        long long _geometryHash = 0;
        int _geometryCount = 0;

        std::shared_ptr<const Placement> _placement;
        mutable std::shared_ptr<const Placement> _cachedFlippedPlacement;

        // Verdict of the last line layout, kept across cache rebuilds (and carried over by
        // snapPlacement) so the readability test below can be hysteretic.
        mutable bool _lineLayoutValid = false;
        mutable bool _lineReversed = false; // the projected run reads right to left, so the glyphs walk the line backwards
        mutable int _lineLayoutFailures = 0;

        mutable bool _cachedValid = false;
        mutable float _cachedScale = 0;
        // The view-projection the cached run was laid out for. The run follows the line as the
        // camera PROJECTS it (see buildLineVertexData), so the whole matrix is the key - a
        // camera that only moved leaves the axes and the scale alone while the perspective
        // compression along the line changes, and the glyphs then walk a road the camera no
        // longer sees that way. Tangram rebuilds its screen transform every frame for the same
        // reason (LabelManager::updateLabelSet -> CurvedLabel::updateScreenTransform); this
        // keeps the frame-to-frame reuse only for a camera that has not moved at all.
        mutable cglib::mat4x4<double> _cachedMVPMatrix = cglib::mat4x4<double>::zero();
        mutable std::shared_ptr<const Placement> _cachedPlacement;
        mutable VertexArray<cglib::vec3<float>> _cachedVertices;
        mutable VertexArray<cglib::vec2<std::int16_t>> _cachedTexCoords;
        mutable VertexArray<cglib::vec4<std::int8_t>> _cachedAttribs;
        mutable VertexArray<std::uint16_t> _cachedIndices;
    };
}

#endif

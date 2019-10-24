/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_LABEL_H_
#define _CARTO_VT_LABEL_H_

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

#include <boost/variant.hpp>
#include <boost/optional.hpp>

namespace carto { namespace vt {
    class Label final {
    public:
        explicit Label(const TileLabel& tileLabel, const cglib::mat4x4<double>& tileMatrix, const std::shared_ptr<const TileTransformer::VertexTransformer>& transformer);

        long long getGlobalId() const { return _globalId; }
        long long getGroupId() const { return _groupId; }
        const std::shared_ptr<const TileLabel::Style>& getStyle() const { return _style; }
        
        cglib::vec3<float> getNormal() const { return _placement ? _placement->normal : cglib::vec3<float>(0, 0, 0); }
        TileId getTileId() const { return _placement ? _placement->tileId : _tileId; }
        long long getLocalId() const { return _placement ? _placement->localId : _localId; }

        bool isValid() const { return (bool) _placement; }

        int getPriority() const { return _priority; }

        float getMinimumGroupDistance() const { return _minimumGroupDistance; }

        float getOpacity() const { return _opacity; }
        void setOpacity(float opacity) { _opacity = opacity; }

        bool isVisible() const { return _visible; }
        void setVisible(bool visible) { _visible = visible; }

        bool isActive() const { return _active; }
        void setActive(bool active) { _active = active; }

        void mergeGeometries(Label& label);
        void snapPlacement(const Label& label);
        bool updatePlacement(const ViewState& viewState);

        bool calculateCenter(cglib::vec3<double>& pos) const;
        bool calculateEnvelope(const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const { return calculateEnvelope((_style->sizeFunc)(viewState), 0, viewState, envelope); }
        bool calculateEnvelope(float size, float buffer, const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const;
        bool calculateVertexData(const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const { return calculateVertexData((_style->sizeFunc)(viewState), viewState, styleIndex, haloStyleIndex, vertices, normals, texCoords, attribs, indices); }
        bool calculateVertexData(float size, const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;

    private:
        constexpr static unsigned int MAX_LABEL_VERTICES = 16384;
        constexpr static unsigned int MAX_LINE_FITTING_ITERATIONS = 1; // number of iterations for line glyph placement on corners

        constexpr static float EXTRA_PLACEMENT_PIXELS = 30.0f; // extra visible pixels required for placement
        constexpr static float SUMMED_ANGLE_SPLIT_THRESHOLD = 2.09f; // maximum sum of segment angles, in radians
        constexpr static float SINGLE_ANGLE_SPLIT_THRESHOLD = 1.57f; // maximum single segment angle, in radians
        constexpr static float MIN_LINE_SEGMENT_DOTPRODUCT = 0.5f; // the minimum allowed dot product between consecutive segments
        constexpr static float MIN_BILLBOARD_VIEW_NORMAL_DOTPRODUCT = 0.49f; // the minimum allowed dot product between view vector and surface normal

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
            cglib::vec3<double> position;
            cglib::vec3<float> normal;
            cglib::vec3<float> xAxis;
            cglib::vec3<float> yAxis;

            explicit Placement(const TileId& tileId, long long localId, const cglib::vec3<double>& pos, const cglib::vec3<float>& norm, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis) : tileId(tileId), localId(localId), edges(), index(0), position(pos), normal(norm), xAxis(xAxis), yAxis(yAxis) { }

            explicit Placement(const TileId& tileId, long long localId, const std::vector<cglib::vec3<double>>& vertices, std::size_t index, const cglib::vec3<double>& pos, const cglib::vec3<float>& norm) : tileId(tileId), localId(localId), edges(), index(index), position(pos), normal(norm), xAxis(0, 0, 0), yAxis(0, 0, 0) {
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
        
        void setupCoordinateSystem(const ViewState& viewState, const std::shared_ptr<const Placement>& placement, cglib::vec3<float>& origin, cglib::vec3<float>& xAxis, cglib::vec3<float>& yAxis) const;
        void buildPointVertexData(VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;
        bool buildLineVertexData(const std::shared_ptr<const Placement>& placement, float scale, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;

        std::shared_ptr<const Placement> getPlacement(const ViewState& viewState) const;
        std::shared_ptr<const Placement> findSnappedPointPlacement(const cglib::vec3<double>& position, const std::list<TilePoint>& tilePoints) const;
        std::shared_ptr<const Placement> findSnappedLinePlacement(const cglib::vec3<double>& position, const std::list<TileLine>& tileLines) const;
        std::shared_ptr<const Placement> findClippedPointPlacement(const ViewState& viewState, const std::list<TilePoint>& tilePoints) const;
        std::shared_ptr<const Placement> findClippedLinePlacement(const ViewState& viewState, const std::list<TileLine>& tileLines) const;

        const TileId _tileId;
        const long long _localId;
        const long long _globalId;
        const long long _groupId;
        const std::vector<Font::Glyph> _glyphs;
        const std::shared_ptr<const TileLabel::Style> _style;
        const int _priority;
        const float _minimumGroupDistance;

        cglib::bbox2<float> _glyphBBox;
        std::list<TilePoint> _tilePoints;
        std::list<TileLine> _tileLines;

        float _opacity = 0.0f;
        bool _visible = false;
        bool _active = false;

        std::shared_ptr<const Placement> _placement;
        mutable std::shared_ptr<const Placement> _cachedFlippedPlacement;

        mutable bool _cachedValid = false;
        mutable float _cachedScale = 0;
        mutable std::shared_ptr<const Placement> _cachedPlacement;
        mutable VertexArray<cglib::vec3<float>> _cachedVertices;
        mutable VertexArray<cglib::vec2<std::int16_t>> _cachedTexCoords;
        mutable VertexArray<cglib::vec4<std::int8_t>> _cachedAttribs;
        mutable VertexArray<std::uint16_t> _cachedIndices;
    };
} }

#endif

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

        const TileId& getTileId() const { return _tileId; }
        long long getLocalId() const { return _localId; }
        long long getGlobalId() const { return _globalId; }
        long long getGroupId() const { return _groupId; }
        const cglib::vec3<float>& getNormal() const { return _normal; }
        const std::shared_ptr<const TileLabel::Style>& getStyle() const { return _style; }
        
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
        bool calculateEnvelope(const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const { return calculateEnvelope((_style->sizeFunc)(viewState), viewState, envelope); }
        bool calculateEnvelope(float size, const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const;
        bool calculateVertexData(const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const { return calculateVertexData((_style->sizeFunc)(viewState), viewState, styleIndex, haloStyleIndex, vertices, normals, texCoords, attribs, indices); }
        bool calculateVertexData(float size, const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;

    private:
        constexpr static unsigned int MAX_LABEL_VERTICES = 16384;
        constexpr static unsigned int MAX_LINE_FITTING_ITERATIONS = 1; // number of iterations for line glyph placement on corners

        constexpr static float EXTRA_PLACEMENT_PIXELS = 30.0f; // extra visible pixels required for placement
        constexpr static float MAX_SINGLE_SEGMENT_ANGLE = 0.7f; // maximum angle between consecutive segments, in radians
        constexpr static float MAX_SUMMED_SEGMENT_ANGLE = 2.0f; // maximum sum of segment angles, in radians
        constexpr static float MIN_BILLBOARD_VIEW_NORMAL_DOTPRODUCT = 0.49f; // the minimum allowed dot product between view vector and surface normal

        using Vertex = cglib::vec3<double>;
        using Vertices = std::vector<Vertex>;
        using VerticesList = std::list<Vertices>;

        struct Placement {
            struct Edge {
                cglib::vec3<float> pos0;
                cglib::vec3<float> pos1;
                cglib::vec3<float> binormal0;
                cglib::vec3<float> binormal1;
                cglib::vec3<float> normal;
                cglib::vec3<float> xAxis;
                cglib::vec3<float> yAxis;
                
                explicit Edge(const cglib::vec3<double>& p0, const cglib::vec3<double>& p1, const cglib::vec3<double>& origin, const cglib::vec3<float>& norm) {
                    pos0 = cglib::vec3<float>::convert(p0 - origin);
                    pos1 = cglib::vec3<float>::convert(p1 - origin);
                    xAxis = cglib::unit(pos1 - pos0);
                    yAxis = cglib::unit(cglib::vector_product(norm, xAxis));
                    binormal0 = yAxis;
                    binormal1 = yAxis;
                    normal = norm;
                }

                void reverse() {
                    std::swap(pos0, pos1);
                    std::swap(binormal0, binormal1);
                    binormal0 = -binormal0;
                    binormal1 = -binormal1;
                    xAxis = -xAxis;
                    yAxis = -yAxis;
                }
            };
            
            std::vector<Edge> edges;
            std::size_t index;
            cglib::vec3<double> pos;
            
            explicit Placement(std::vector<Edge> baseEdges, std::size_t index, const cglib::vec3<double>& pos) : edges(std::move(baseEdges)), index(index), pos(pos) {
                for (std::size_t i = 1; i < edges.size(); i++) {
                    cglib::vec3<float> binormal = edges[i - 1].yAxis + edges[i].yAxis;
                    if (cglib::norm(binormal) != 0) {
                        binormal = cglib::unit(binormal);
                        edges[i - 1].binormal1 = edges[i].binormal0 = binormal * (1.0f / cglib::dot_product(edges[i - 1].yAxis, binormal));
                    }
                }
            }

            void reverse() {
                index = edges.size() - 1 - index;
                std::reverse(edges.begin(), edges.end());
                std::for_each(edges.begin(), edges.end(), [](Edge& edge) { edge.reverse(); });
            }
        };
        
        void setupCoordinateSystem(const ViewState& viewState, const std::shared_ptr<const Placement>& placement, cglib::vec3<float>& origin, cglib::vec3<float>& xAxis, cglib::vec3<float>& yAxis) const;
        void buildPointVertexData(VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;
        bool buildLineVertexData(const std::shared_ptr<const Placement>& placement, float scale, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;

        std::shared_ptr<const Placement> getPlacement(const ViewState& viewState) const;
        std::shared_ptr<const Placement> reversePlacement(const std::shared_ptr<const Placement>& placement) const;
        std::shared_ptr<const Placement> findSnappedPointPlacement(const Vertex& position, const Vertices& vertices) const;
        std::shared_ptr<const Placement> findSnappedLinePlacement(const Vertex& position, const VerticesList& verticesList) const;
        std::shared_ptr<const Placement> findClippedPointPlacement(const ViewState& viewState, const Vertices& vertices) const;
        std::shared_ptr<const Placement> findClippedLinePlacement(const ViewState& viewState, const VerticesList& verticesList) const;

        const TileId _tileId;
        const long long _localId;
        const long long _globalId;
        const long long _groupId;
        const std::vector<Font::Glyph> _glyphs;
        const std::shared_ptr<const TileLabel::Style> _style;
        const int _priority;
        const float _minimumGroupDistance;

        cglib::bbox2<float> _glyphBBox;
        cglib::vec3<float> _normal;
        Vertices _position;
        VerticesList _verticesList;

        float _opacity = 0.0f;
        bool _visible = false;
        bool _active = false;

        std::shared_ptr<const Placement> _placement;
        std::shared_ptr<const Placement> _flippedPlacement;

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

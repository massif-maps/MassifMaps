/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILEGEOMETRYITERATOR_H_
#define _CARTO_VT_TILEGEOMETRYITERATOR_H_

#include "TileId.h"
#include "Tile.h"
#include "TileGeometry.h"
#include "TileTransformer.h"

#include <memory>
#include <array>
#include <vector>

#include <cglib/mat.h>

namespace carto::vt {
    class TileGeometryIterator final {
    public:
        using TriangleCoords = std::array<cglib::vec3<float>, 3>;
        using TriangleUVs = std::array<cglib::vec2<float>, 3>;

        TileGeometryIterator() = delete;
        explicit TileGeometryIterator(const TileId& tileId, const std::shared_ptr<const TileGeometry>& geometry, const std::shared_ptr<const TileTransformer>& transformer, const ViewState& viewState, float pointBuffer, float lineBuffer, float scale, float heightScale);

        operator bool() const { return _index + 2 < _geometry->getIndices().size(); }
        long long id() const { return getId(_index); }
        cglib::vec4<std::int8_t> attribs() const { return getAttribs(_index); }
        TriangleCoords triangleCoords() const { return getTriangleCoords(_index); }
        TriangleUVs triangleUVs() const { return getTriangleUVs(_index); }
        TileGeometryIterator& operator++ () { _index += 3; return *this; }

    private:
        long long getId(std::size_t index) const;
        cglib::vec4<std::int8_t> getAttribs(std::size_t index) const;
        TriangleCoords getTriangleCoords(std::size_t index) const;
        TriangleUVs getTriangleUVs(std::size_t index) const;

        cglib::vec3<float> decodeVertexPos(std::size_t index) const;
        cglib::vec2<float> decodeVertexUV(std::size_t index) const;
        cglib::vec3<float> decodePointOffset(std::size_t index) const;
        cglib::vec3<float> decodeLineOffset(std::size_t index) const;
        cglib::vec3<float> decodePolygon3DOffset(std::size_t index) const;

        ViewState _viewState;
        float _pointBuffer;
        float _lineBuffer;
        float _scale;
        float _heightScale;
        std::size_t _index = 0;
        std::shared_ptr<const TileGeometry> _geometry;
        cglib::mat4x4<float> _transformMatrix;
    };
}

#endif

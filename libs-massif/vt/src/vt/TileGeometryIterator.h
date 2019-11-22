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

#include <boost/optional.hpp>

namespace carto { namespace vt {
    class TileGeometryIterator final {
    public:
        using Triangle = std::array<cglib::vec3<float>, 3>;
        
        TileGeometryIterator() = delete;
        explicit TileGeometryIterator(const TileId& tileId, const std::shared_ptr<const Tile>& tile, const std::shared_ptr<const TileGeometry>& geometry, const std::shared_ptr<const TileTransformer>& transformer, const ViewState& viewState, float buffer, float scale, float heightScale);

        operator bool() const { return _index + 2 < _geometry->getIndices().size(); }
        long long id() const { return getId(_index); }
        Triangle triangle() const { return getTriangle(_index); }
        TileGeometryIterator& operator++ () { _index += 3; return *this; }

    private:
        long long getId(std::size_t index) const;
        Triangle getTriangle(std::size_t index) const;

        cglib::vec3<float> decodeVertex(std::size_t index) const;
        cglib::vec3<float> decodePointOffset(std::size_t index) const;
        cglib::vec3<float> decodeLineOffset(std::size_t index) const;
        cglib::vec3<float> decodePolygon3DOffset(std::size_t index) const;

        ViewState _viewState;
        float _buffer;
        float _scale;
        float _heightScale;
        std::size_t _index = 0;
        std::shared_ptr<const TileGeometry> _geometry;
        cglib::mat4x4<float> _transformMatrix;
    };
} }

#endif

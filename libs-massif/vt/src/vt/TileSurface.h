/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILESURFACE_H_
#define _CARTO_VT_TILESURFACE_H_

#include "Bitmap.h"
#include "Color.h"
#include "StrokeMap.h"
#include "VertexArray.h"
#include "Styles.h"

#include <memory>

namespace carto { namespace vt {
    class TileSurface final {
    public:
        struct VertexGeometryLayoutParameters {
            int vertexSize;
            int coordOffset;
            int texCoordOffset;
            int normalOffset;
            int binormalOffset;

            VertexGeometryLayoutParameters() : vertexSize(0), coordOffset(-1), texCoordOffset(-1), normalOffset(-1), binormalOffset(-1) { }
        };

        explicit TileSurface(const VertexGeometryLayoutParameters& vertexGeometryLayoutParameters, VertexArray<std::uint8_t> vertexGeometry, VertexArray<std::uint16_t> indices) : _vertexGeometryLayoutParameters(vertexGeometryLayoutParameters), _indicesCount(static_cast<unsigned int>(indices.size())), _vertexGeometry(std::move(vertexGeometry)), _indices(std::move(indices)) { }

        const VertexGeometryLayoutParameters& getVertexGeometryLayoutParameters() const { return _vertexGeometryLayoutParameters; }
        unsigned int getIndicesCount() const { return _indicesCount; }

        const VertexArray<std::uint8_t>& getVertexGeometry() const { return _vertexGeometry; }
        const VertexArray<std::uint16_t>& getIndices() const { return _indices; }

        void releaseVertexArrays() {
            _vertexGeometry.clear();
            _vertexGeometry.shrink_to_fit();
            _indices.clear();
            _indices.shrink_to_fit();
        }

        std::size_t getResidentSize() const {
            return 16 + _vertexGeometry.size() * sizeof(std::uint8_t) + _indices.size() * sizeof(std::uint16_t);
        }

    private:
        const VertexGeometryLayoutParameters _vertexGeometryLayoutParameters;
        const unsigned int _indicesCount; // real count, even if indices are released

        VertexArray<std::uint8_t> _vertexGeometry;
        VertexArray<std::uint16_t> _indices;
    };
} }

#endif

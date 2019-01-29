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

            VertexGeometryLayoutParameters() : vertexSize(0), coordOffset(-1), texCoordOffset(-1), normalOffset(-1) { }
        };

        explicit TileSurface(const VertexGeometryLayoutParameters& vertexGeometryLayoutParameters, VertexArray<unsigned char> vertexGeometry, VertexArray<unsigned short> indices) : _vertexGeometryLayoutParameters(vertexGeometryLayoutParameters), _indicesCount(static_cast<unsigned int>(indices.size())), _vertexGeometry(std::move(vertexGeometry)), _indices(std::move(indices)) { }

        const VertexGeometryLayoutParameters& getVertexGeometryLayoutParameters() const { return _vertexGeometryLayoutParameters; }
        unsigned int getIndicesCount() const { return _indicesCount; }

        const VertexArray<unsigned char>& getVertexGeometry() const { return _vertexGeometry; }
        const VertexArray<unsigned short>& getIndices() const { return _indices; }

        void releaseVertexArrays() {
            _vertexGeometry.clear();
            _vertexGeometry.shrink_to_fit();
            _indices.clear();
            _indices.shrink_to_fit();
        }

        std::size_t getResidentSize() const {
            return 16 + _vertexGeometry.size() * sizeof(unsigned char) + _indices.size() * sizeof(unsigned short);
        }

    private:
        const VertexGeometryLayoutParameters _vertexGeometryLayoutParameters;
        const unsigned int _indicesCount; // real count, even if indices are released

        VertexArray<unsigned char> _vertexGeometry;
        VertexArray<unsigned short> _indices;
    };
} }

#endif

/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILESURFACEBUILDER_H_
#define _CARTO_VT_TILESURFACEBUILDER_H_

#include "TileId.h"
#include "TileTransformer.h"
#include "TileSurface.h"
#include "PoolAllocator.h"
#include "VertexArray.h"

#include <cstdint>
#include <memory>
#include <set>
#include <map>
#include <vector>

#include <cglib/vec.h>

namespace carto { namespace vt {
    class TileSurfaceBuilder final {
    public:
        explicit TileSurfaceBuilder(std::shared_ptr<const TileTransformer> transformer);

        void setOrigin(const cglib::vec3<double>& origin);
        void setVisibleTiles(const std::set<TileId>& tileIds);

        std::vector<std::shared_ptr<TileSurface>> buildTileSurface(const TileId& tileId) const;

    private:
        using TileNeighbours = std::array<std::vector<TileId>, 4>; // left, right, up, down
        
        constexpr static unsigned int RESERVED_VERTICES = 8192;

        void buildTileGeometry(const TileId& tileId, const std::array<std::vector<TileId>, 4>& vertexIds, VertexArray<cglib::vec2<float>>& coords2D, VertexArray<cglib::vec3<float>>& coords3D, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec3<float>>& binormals, VertexArray<std::size_t>& indices) const;
        void buildPoleGeometry(int poleZ, const std::vector<TileId>& vertexIds, VertexArray<cglib::vec2<float>>& coords2D, VertexArray<cglib::vec3<float>>& coords3D, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec3<float>>& binormals, VertexArray<std::size_t>& indices) const;

        void packGeometry(const VertexArray<cglib::vec3<float>>& coords, const VertexArray<cglib::vec2<float>>& texCoords, const VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec3<float>>& binormals, const VertexArray<std::size_t>& indices, std::vector<std::shared_ptr<TileSurface>>& tileSurfaces) const;

        static std::vector<TileId> tesselateTile(const TileId& baseTileId, const std::vector<TileId>& tileIds, bool xCoord);

        std::map<TileId, TileNeighbours> _tileSplitNeighbours;
        cglib::vec3<double> _origin = cglib::vec3<double>(0, 0, 0);

        mutable std::map<TileId, std::vector<std::shared_ptr<TileSurface>>> _tileSurfaceCache;

        const std::shared_ptr<const TileTransformer> _transformer;
    };
} }

#endif

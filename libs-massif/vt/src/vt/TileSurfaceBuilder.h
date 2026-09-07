/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_TILESURFACEBUILDER_H_
#define _MASSIF_VT_TILESURFACEBUILDER_H_

#include "TileId.h"
#include "TileTransformer.h"
#include "TileSurface.h"
#include "VertexArray.h"

#include <cstdint>
#include <memory>
#include <set>
#include <map>
#include <vector>

#include <cglib/vec.h>

namespace massif::vt {
    class TileSurfaceBuilder final {
    public:
        explicit TileSurfaceBuilder(std::shared_ptr<const TileTransformer> transformer);

        void setOrigin(const cglib::vec3<double>& origin);
        void setVisibleTiles(const std::set<TileId>& tileIds);
        void setTerrainSkirts(bool terrainSkirts);
        void invalidateCaches();
        // Drops only the cached surfaces overlapping one of the given tiles (in either
        // direction - an elevation tile may be coarser or finer than the surface tile).
        void invalidateCaches(const std::vector<TileId>& tileIds);

        std::vector<std::shared_ptr<TileSurface>> buildTileSurface(const TileId& tileId) const;

        // Builds a single shared unit-grid surface (tile-local [0,1] coordinates, flat
        // planar geometry) with a fixed resolution x resolution subdivision. Unlike
        // buildTileSurface this carries no per-tile world placement: it is drawn for every
        // tile with the tile's own MVP + terrain uniforms (exactly like draped geometry),
        // so the mesh can be built ONCE and reused across all tiles - no per-tile
        // tesselation, no per-tile VBO (tangram's shared raster grid model). Terrain is
        // planar only, so the flat transformer's constant normal/binormal are baked in.
        std::shared_ptr<TileSurface> buildRegularGridSurface(int resolution) const;

    private:
        using TileNeighbours = std::array<std::vector<TileId>, 4>; // left, right, up, down
        
        static constexpr unsigned int RESERVED_VERTICES = 8192;
        static constexpr float SKIRT_SENTINEL = -1000000.0f; // skirt bottom z = SKIRT_SENTINEL - drop (decoded in the terrain vertex shader)
        static constexpr float SKIRT_DEPTH = 0.02f; // skirt extrusion depth, relative to the tile size

        void buildTileGeometry(const TileId& tileId, const std::array<std::vector<TileId>, 4>& vertexIds, VertexArray<cglib::vec2<float>>& coords2D, VertexArray<cglib::vec3<float>>& coords3D, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec3<float>>& binormals, VertexArray<std::size_t>& indices) const;
        void buildPoleGeometry(int poleZ, const std::vector<TileId>& vertexIds, VertexArray<cglib::vec2<float>>& coords2D, VertexArray<cglib::vec3<float>>& coords3D, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec3<float>>& binormals, VertexArray<std::size_t>& indices) const;

        void packGeometry(const VertexArray<cglib::vec3<float>>& coords, const VertexArray<cglib::vec2<float>>& texCoords, const VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec3<float>>& binormals, const VertexArray<std::size_t>& indices, std::vector<std::shared_ptr<TileSurface>>& tileSurfaces) const;

        static std::vector<TileId> tesselateTile(const TileId& baseTileId, const std::vector<TileId>& tileIds, bool xCoord);

        std::map<TileId, TileNeighbours> _tileSplitNeighbours;
        cglib::vec3<double> _origin = cglib::vec3<double>(0, 0, 0);
        bool _terrainSkirts = false;

        mutable std::map<TileId, std::vector<std::shared_ptr<TileSurface>>> _tileSurfaceCache;

        const std::shared_ptr<const TileTransformer> _transformer;
    };
}

#endif

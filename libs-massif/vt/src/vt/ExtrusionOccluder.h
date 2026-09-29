/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_EXTRUSIONOCCLUDER_H_
#define _MASSIF_VT_EXTRUSIONOCCLUDER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include <cglib/vec.h>

namespace massif::vt {
    class TileGeometry;

    /**
     * An extrusion mesh kept for the labels' CPU occlusion rays (06-labels.mdx). Tests run in the tile's frame:
     * x, y tile-local, z world, a vertex at its base plus height units times heightScale, as polygon3DVsh.
     */
    class ExtrusionOccluder final {
    public:
        // Null for a geometry with no extruded triangle.
        static std::shared_ptr<const ExtrusionOccluder> build(const TileGeometry& geometry);

        // Whether p(t) = origin + t * dir hits a triangle for t in (t0, t1). groundZ stands in for bases the
        // geometry does not carry; a triangle on an unresolved base does not occlude.
        bool intersects(const cglib::vec3<double>& origin, const cglib::vec3<double>& dir, double t0, double t1, const TileGeometry& geometry, double heightScale, double groundZ) const;

    private:
        static constexpr int GRID_SIZE = 32;

        double vertexBase(const TileGeometry& geometry, std::uint32_t vertex, double groundZ) const;
        double maxBase(const TileGeometry& geometry, double groundZ) const;
        bool intersectsTriangle(std::uint32_t triangle, const cglib::vec3<double>& origin, const cglib::vec3<double>& dir, double t0, double t1, const TileGeometry& geometry, double heightScale, double groundZ) const;

        std::vector<float> _x, _y, _h;         // per vertex: tile-local position, raw height units
        std::vector<std::uint16_t> _triangles; // three vertex indices each
        std::vector<std::uint32_t> _cellStart; // GRID_SIZE^2 + 1 offsets into _cellTriangles
        std::vector<std::uint32_t> _cellTriangles;
        float _minX = 0, _minY = 0, _cellX = 1, _cellY = 1;
        float _maxHeight = 0;
        // Bases change when a DEM tile lands; the highest is recomputed then (GL thread only).
        mutable double _maxBase = 0;
        mutable unsigned int _maxBaseVersion = ~0u;
    };
}

#endif

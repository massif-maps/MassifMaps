#include "ExtrusionOccluder.h"
#include "TileGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace massif::vt {

    std::shared_ptr<const ExtrusionOccluder> ExtrusionOccluder::build(const TileGeometry& geometry) {
        const TileGeometry::VertexGeometryLayoutParameters& params = geometry.getVertexGeometryLayoutParameters();
        const VertexArray<std::uint8_t>& vertexGeometry = geometry.getVertexGeometry();
        const VertexArray<std::uint16_t>& indices = geometry.getIndices();
        if (geometry.getType() != TileGeometry::Type::POLYGON3D || params.coordOffset < 0 || params.heightOffset < 0 || !(params.coordScale > 0) || params.vertexSize <= 0 || indices.empty()) {
            return std::shared_ptr<const ExtrusionOccluder>();
        }
        auto occluder = std::make_shared<ExtrusionOccluder>();
        std::size_t vertexCount = vertexGeometry.size() / params.vertexSize;
        occluder->_x.resize(vertexCount);
        occluder->_y.resize(vertexCount);
        occluder->_h.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; i++) {
            const std::uint8_t* vertex = vertexGeometry.data() + i * params.vertexSize;
            std::int16_t coord[2], height;
            std::memcpy(coord, vertex + params.coordOffset, sizeof(coord));
            std::memcpy(&height, vertex + params.heightOffset, sizeof(height));
            occluder->_x[i] = coord[0] / params.coordScale;
            occluder->_y[i] = coord[1] / params.coordScale;
            occluder->_h[i] = height;
        }

        // Only what stands above its base: a footprint's floor never occludes a label.
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            if (indices[i] >= vertexCount || indices[i + 1] >= vertexCount || indices[i + 2] >= vertexCount) {
                continue;
            }
            if (occluder->_h[indices[i]] > 0 || occluder->_h[indices[i + 1]] > 0 || occluder->_h[indices[i + 2]] > 0) {
                occluder->_triangles.insert(occluder->_triangles.end(), { indices[i], indices[i + 1], indices[i + 2] });
            }
        }
        if (occluder->_triangles.empty()) {
            return std::shared_ptr<const ExtrusionOccluder>();
        }

        float maxX = occluder->_x[occluder->_triangles[0]], maxY = occluder->_y[occluder->_triangles[0]];
        occluder->_minX = maxX;
        occluder->_minY = maxY;
        for (std::uint16_t v : occluder->_triangles) {
            occluder->_minX = std::min(occluder->_minX, occluder->_x[v]);
            occluder->_minY = std::min(occluder->_minY, occluder->_y[v]);
            maxX = std::max(maxX, occluder->_x[v]);
            maxY = std::max(maxY, occluder->_y[v]);
            occluder->_maxHeight = std::max(occluder->_maxHeight, occluder->_h[v]);
        }
        occluder->_cellX = std::max(1.0e-6f, (maxX - occluder->_minX) / GRID_SIZE);
        occluder->_cellY = std::max(1.0e-6f, (maxY - occluder->_minY) / GRID_SIZE);

        // Each triangle in every cell its box touches; counted first so the lists are one array.
        std::size_t triangleCount = occluder->_triangles.size() / 3;
        std::vector<std::array<int, 4>> ranges(triangleCount);
        occluder->_cellStart.assign(GRID_SIZE * GRID_SIZE + 1, 0);
        for (std::size_t t = 0; t < triangleCount; t++) {
            float x0 = occluder->_x[occluder->_triangles[t * 3]], x1 = x0, y0 = occluder->_y[occluder->_triangles[t * 3]], y1 = y0;
            for (int k = 1; k < 3; k++) {
                std::uint16_t v = occluder->_triangles[t * 3 + k];
                x0 = std::min(x0, occluder->_x[v]);
                x1 = std::max(x1, occluder->_x[v]);
                y0 = std::min(y0, occluder->_y[v]);
                y1 = std::max(y1, occluder->_y[v]);
            }
            auto cell = [](float value, float min, float size) { return std::min(GRID_SIZE - 1, std::max(0, static_cast<int>((value - min) / size))); };
            ranges[t] = { cell(x0, occluder->_minX, occluder->_cellX), cell(x1, occluder->_minX, occluder->_cellX), cell(y0, occluder->_minY, occluder->_cellY), cell(y1, occluder->_minY, occluder->_cellY) };
            for (int cy = ranges[t][2]; cy <= ranges[t][3]; cy++) {
                for (int cx = ranges[t][0]; cx <= ranges[t][1]; cx++) {
                    occluder->_cellStart[cy * GRID_SIZE + cx + 1]++;
                }
            }
        }
        for (std::size_t c = 1; c < occluder->_cellStart.size(); c++) {
            occluder->_cellStart[c] += occluder->_cellStart[c - 1];
        }
        occluder->_cellTriangles.resize(occluder->_cellStart.back());
        std::vector<std::uint32_t> fill(occluder->_cellStart.begin(), occluder->_cellStart.end() - 1);
        for (std::size_t t = 0; t < triangleCount; t++) {
            for (int cy = ranges[t][2]; cy <= ranges[t][3]; cy++) {
                for (int cx = ranges[t][0]; cx <= ranges[t][1]; cx++) {
                    occluder->_cellTriangles[fill[cy * GRID_SIZE + cx]++] = static_cast<std::uint32_t>(t);
                }
            }
        }
        return occluder;
    }

    double ExtrusionOccluder::vertexBase(const TileGeometry& geometry, std::uint32_t vertex, double groundZ) const {
        const TileGeometry::VertexGeometryLayoutParameters& params = geometry.getVertexGeometryLayoutParameters();
        const VertexArray<std::uint8_t>& vertexGeometry = geometry.getVertexGeometry();
        std::size_t offset = static_cast<std::size_t>(vertex) * params.vertexSize + params.baseOffset;
        if (params.baseOffset < 0 || offset + sizeof(float) > vertexGeometry.size()) {
            return groundZ;
        }
        float base;
        std::memcpy(&base, vertexGeometry.data() + offset, sizeof(float));
        return base;
    }

    double ExtrusionOccluder::maxBase(const TileGeometry& geometry, double groundZ) const {
        if (geometry.getVertexGeometryLayoutParameters().baseOffset < 0) {
            return groundZ;
        }
        unsigned int version = geometry.getBaseElevationVersion() * 2 + (geometry.isBaseResolved() ? 1 : 0);
        if (version != _maxBaseVersion) {
            _maxBaseVersion = version;
            _maxBase = TileGeometry::UNRESOLVED_BASE;
            for (std::uint16_t v : _triangles) {
                _maxBase = std::max(_maxBase, vertexBase(geometry, v, groundZ));
            }
        }
        return _maxBase;
    }

    bool ExtrusionOccluder::intersectsTriangle(std::uint32_t triangle, const cglib::vec3<double>& origin, const cglib::vec3<double>& dir, double t0, double t1, const TileGeometry& geometry, double heightScale, double groundZ) const {
        cglib::vec3<double> p[3];
        for (int k = 0; k < 3; k++) {
            std::uint16_t v = _triangles[triangle * 3 + k];
            double base = vertexBase(geometry, v, groundZ);
            if (base < -1.0e29) {
                return false;
            }
            p[k] = cglib::vec3<double>(_x[v], _y[v], base + _h[v] * heightScale);
        }
        // Moller-Trumbore, both faces: a wall seen from inside a courtyard occludes too.
        cglib::vec3<double> e1 = p[1] - p[0], e2 = p[2] - p[0];
        cglib::vec3<double> pv = cglib::vector_product(dir, e2);
        double det = cglib::dot_product(e1, pv);
        if (std::abs(det) < 1.0e-18) {
            return false;
        }
        double inv = 1.0 / det;
        cglib::vec3<double> tv = origin - p[0];
        double u = cglib::dot_product(tv, pv) * inv;
        if (u < 0.0 || u > 1.0) {
            return false;
        }
        cglib::vec3<double> qv = cglib::vector_product(tv, e1);
        double w = cglib::dot_product(dir, qv) * inv;
        if (w < 0.0 || u + w > 1.0) {
            return false;
        }
        double t = cglib::dot_product(e2, qv) * inv;
        return t > t0 && t < t1;
    }

    bool ExtrusionOccluder::intersects(const cglib::vec3<double>& origin, const cglib::vec3<double>& dir, double t0, double t1, const TileGeometry& geometry, double heightScale, double groundZ) const {
        // Only the stretch of the ray below the tallest roof can meet a wall. The walk is clipped; a hit
        // is judged against the caller's range, as a wall ON the grid's edge clips it to that very t.
        const double hitT0 = t0, hitT1 = t1;
        double top = maxBase(geometry, groundZ) + _maxHeight * heightScale;
        if (dir(2) < 0) {
            t0 = std::max(t0, (top - origin(2)) / dir(2));
        } else if (dir(2) > 0) {
            t1 = std::min(t1, (top - origin(2)) / dir(2));
        } else if (origin(2) > top) {
            return false;
        }
        // Then to the grid's box, a slab per axis.
        double maxX = _minX + _cellX * GRID_SIZE, maxY = _minY + _cellY * GRID_SIZE;
        const double lo[2] = { _minX, _minY }, hi[2] = { maxX, maxY };
        for (int axis = 0; axis < 2; axis++) {
            if (std::abs(dir(axis)) < 1.0e-18) {
                if (origin(axis) < lo[axis] || origin(axis) > hi[axis]) {
                    return false;
                }
                continue;
            }
            double ta = (lo[axis] - origin(axis)) / dir(axis), tb = (hi[axis] - origin(axis)) / dir(axis);
            t0 = std::max(t0, std::min(ta, tb));
            t1 = std::min(t1, std::max(ta, tb));
        }
        if (!(t0 < t1)) {
            return false;
        }

        // Amanatides-Woo walk over the cells the ray's shadow on the grid crosses.
        cglib::vec3<double> start = origin + dir * t0;
        int cx = std::min(GRID_SIZE - 1, std::max(0, static_cast<int>((start(0) - _minX) / _cellX)));
        int cy = std::min(GRID_SIZE - 1, std::max(0, static_cast<int>((start(1) - _minY) / _cellY)));
        int stepX = (dir(0) > 0 ? 1 : -1), stepY = (dir(1) > 0 ? 1 : -1);
        auto boundary = [&](int cell, int step, double min, double size, int axis) {
            if (std::abs(dir(axis)) < 1.0e-18) {
                return std::numeric_limits<double>::infinity();
            }
            double edge = min + (cell + (step > 0 ? 1 : 0)) * size;
            return (edge - origin(axis)) / dir(axis);
        };
        double nextX = boundary(cx, stepX, _minX, _cellX, 0), nextY = boundary(cy, stepY, _minY, _cellY, 1);
        double deltaX = (std::abs(dir(0)) < 1.0e-18 ? std::numeric_limits<double>::infinity() : _cellX / std::abs(dir(0)));
        double deltaY = (std::abs(dir(1)) < 1.0e-18 ? std::numeric_limits<double>::infinity() : _cellY / std::abs(dir(1)));
        while (cx >= 0 && cx < GRID_SIZE && cy >= 0 && cy < GRID_SIZE) {
            int cell = cy * GRID_SIZE + cx;
            for (std::uint32_t i = _cellStart[cell]; i < _cellStart[cell + 1]; i++) {
                if (intersectsTriangle(_cellTriangles[i], origin, dir, hitT0, hitT1, geometry, heightScale, groundZ)) {
                    return true;
                }
            }
            if (std::min(nextX, nextY) > t1) {
                break;
            }
            if (nextX < nextY) {
                cx += stepX;
                nextX += deltaX;
            } else {
                cy += stepY;
                nextY += deltaY;
            }
        }
        return false;
    }
}

#include "TileTransformer.h"

namespace carto { namespace vt {
    DefaultTileTransformer::DefaultVertexTransformer::DefaultVertexTransformer(const TileId& tileId, float scale) :
        _tileId(tileId), _scale(scale)
    {
    }

    cglib::vec3<float> DefaultTileTransformer::DefaultVertexTransformer::calculatePoint(const cglib::vec2<float>& pos) const {
        return cglib::vec3<float>(pos(0), 1 - pos(1), 0);
    }

    cglib::vec3<float> DefaultTileTransformer::DefaultVertexTransformer::calculateNormal(const cglib::vec2<float>& pos) const {
        return cglib::vec3<float>(0, 0, 1);
    }

    cglib::vec3<float> DefaultTileTransformer::DefaultVertexTransformer::calculateVector(const cglib::vec2<float>& pos, const cglib::vec2<float>& vec) const {
        return cglib::vec3<float>(vec(0), -vec(1), 0);
    }

    cglib::vec2<float> DefaultTileTransformer::DefaultVertexTransformer::calculateTilePosition(const cglib::vec3<float>& pos) const {
        return cglib::vec2<float>(pos(0), 1 - pos(1));
    }

    float DefaultTileTransformer::DefaultVertexTransformer::calculateHeight(const cglib::vec2<float>& pos, float height) const {
        double normY = 2 * PI * ((_tileId.y + 1 - pos(1)) / (1 << _tileId.zoom) - 0.5);
        double latitude = PI * 0.5 - 2 * std::atan(std::exp(normY));
        return static_cast<float>(height / std::cos(latitude) * (1 << _tileId.zoom) / EARTH_CIRCUMFERENCE);
    }

    void DefaultTileTransformer::DefaultVertexTransformer::tesselateLineString(const cglib::vec2<float>* points, std::size_t count, VertexArray<cglib::vec2<float>>& tesselatedPoints) const {
        for (std::size_t i = 0; i < count; i++) {
            tesselatedPoints.append(points[i]);
        }
    }

    void DefaultTileTransformer::DefaultVertexTransformer::tesselateTriangles(const unsigned int* indices, std::size_t count, VertexArray<cglib::vec2<float>>& coords, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<unsigned int>& tesselatedIndices) const {
        for (std::size_t i = 0; i < count; i++) {
            tesselatedIndices.append(indices[i]);
        }
    }

    cglib::vec3<double> DefaultTileTransformer::calculateTileOrigin(const TileId& tileId) const {
        int tileMask = (1 << tileId.zoom) - 1;
        double zoomScale = 1.0 / (1 << tileId.zoom);
        cglib::vec3<double> p;
        p(0) = (tileId.x * zoomScale - 0.5) * _scale;
        p(1) = ((tileMask - tileId.y) * zoomScale - 0.5) * _scale;
        p(2) = 0;
        return p;
    }

    cglib::bbox3<double> DefaultTileTransformer::calculateTileBBox(const TileId& tileId) const {
        return cglib::transform_bbox(cglib::bbox3<double>(cglib::vec3<double>(0, 0, 0), cglib::vec3<double>(1, 1, 0)), calculateTileMatrix(tileId, 1.0f));
    }

    cglib::mat4x4<double> DefaultTileTransformer::calculateTileMatrix(const TileId& tileId, float coordScale) const {
        double s = _scale * coordScale / (1 << tileId.zoom);
        cglib::vec3<double> p = calculateTileOrigin(tileId);

        cglib::mat4x4<double> m = cglib::mat4x4<double>::zero();
        m(0, 0) = s;
        m(1, 1) = s;
        m(2, 2) = s;
        m(0, 3) = p(0);
        m(1, 3) = p(1);
        m(2, 3) = p(2);
        m(3, 3) = 1;
        return m;
    }

    cglib::mat4x4<float> DefaultTileTransformer::calculateTileTransform(const TileId& tileId, const cglib::vec2<float>& translate, float coordScale) const {
        return cglib::translate4_matrix(cglib::vec3<float>(translate(0) / coordScale, -translate(1) / coordScale, 0));
    }

    std::shared_ptr<const TileTransformer::VertexTransformer> DefaultTileTransformer::createTileVertexTransformer(const TileId& tileId) const {
        return std::make_shared<DefaultVertexTransformer>(tileId, _scale);
    }

    SphericalTileTransformer::SphericalVertexTransformer::SphericalVertexTransformer(const TileId& tileId, float scale, const cglib::vec3<double>& origin, float divideThreshold) :
        _tileId(tileId), _scale(scale), _origin(origin * (1.0 / _scale)), _divideThreshold(divideThreshold), _tileOffset(tileOffset(tileId)), _tileScale(tileScale(tileId))
    {
    }

    cglib::vec3<float> SphericalTileTransformer::SphericalVertexTransformer::calculatePoint(const cglib::vec2<float>& pos) const {
        if (pos(0) > 0 && pos(0) < 1 && pos(1) > 0 && pos(1) < 1) {
            cglib::vec3<double> p = tileToSpherical(pos);
            return cglib::vec3<float>::convert((p - _origin) * static_cast<double>(1 << _tileId.zoom));
        }
        
        // Hack: for points at tile borders (or crossing them) discretesize tile borders with triangles. This avoids cracks between tiles.
        int gridSize = std::max(1, ZOOM_0_GRID_SIZE / (1 << _tileId.zoom));
        float x = std::floor(pos(0) * gridSize);
        float y = std::floor(pos(1) * gridSize);
        double u = pos(0) * gridSize - x;
        double v = pos(1) * gridSize - y;
        cglib::vec3<double> p00 = tileToSpherical(cglib::vec2<float>((x + 0) / gridSize, (y + 0) / gridSize));
        cglib::vec3<double> p11 = tileToSpherical(cglib::vec2<float>((x + 1) / gridSize, (y + 1) / gridSize));
        cglib::vec3<double> du;
        cglib::vec3<double> dv;
        if (u > v) {
            cglib::vec3<double> p10 = tileToSpherical(cglib::vec2<float>((x + 1) / gridSize, (y + 0) / gridSize));
            du = p10 - p00;
            dv = p11 - p10;
        }
        else {
            cglib::vec3<double> p01 = tileToSpherical(cglib::vec2<float>((x + 0) / gridSize, (y + 1) / gridSize));
            dv = p01 - p00;
            du = p11 - p01;
        }
        return cglib::vec3<float>::convert((p00 + du * u + dv * v - _origin) * static_cast<double>(1 << _tileId.zoom));
    }

    cglib::vec3<float> SphericalTileTransformer::SphericalVertexTransformer::calculateNormal(const cglib::vec2<float>& pos) const {
        return cglib::vec3<float>::convert(tileToSpherical(pos));
    }

    cglib::vec3<float> SphericalTileTransformer::SphericalVertexTransformer::calculateVector(const cglib::vec2<float>& pos, const cglib::vec2<float>& vec) const {
        cglib::vec3<double> p = tileToSpherical(pos);

        float x = static_cast<float>(p(0));
        float y = static_cast<float>(p(1));
        float z = static_cast<float>(p(2));
        
        float scale = static_cast<float>(PI) / std::sqrt(x * x + y * y);

        float dx_du = -y;
        float dy_du = x;
        float dz_du = 0;

        float dx_dv = z * x;
        float dy_dv = z * y;
        float dz_dv = -(x * x + y * y);

        cglib::vec3<float> v;
        v(0) = (dx_du * vec(0) + dx_dv * vec(1)) * scale;
        v(1) = (dy_du * vec(0) + dy_dv * vec(1)) * scale;
        v(2) = (dz_du * vec(0) + dz_dv * vec(1)) * scale;
        return v;
    }

    cglib::vec2<float> SphericalTileTransformer::SphericalVertexTransformer::calculateTilePosition(const cglib::vec3<float>& pos) const {
        cglib::vec3<double> sphericalPos = cglib::vec3<double>::convert(pos) * (1.0 / (1 << _tileId.zoom)) + _origin;
        return sphericalToTile(sphericalPos);
    }

    float SphericalTileTransformer::SphericalVertexTransformer::calculateHeight(const cglib::vec2<float>& pos, float height) const {
        return static_cast<float>(height * (1 << _tileId.zoom) / EARTH_CIRCUMFERENCE * 2 * PI);
    }

    void SphericalTileTransformer::SphericalVertexTransformer::tesselateLineString(const cglib::vec2<float>* points, std::size_t count, VertexArray<cglib::vec2<float>>& tesselatedPoints) const {
        if (count > 0) {
            tesselatedPoints.append(points[0]);
            for (std::size_t i = 0; i + 1 < count; i++) {
                const cglib::vec2<float>& pos0 = points[i + 0];
                const cglib::vec2<float>& pos1 = points[i + 1];
                float dist = cglib::length(pos1 - pos0) * static_cast<float>(_tileScale);
                tesselateSegment(pos0, pos1, dist, tesselatedPoints);
            }
        }
    }

    void SphericalTileTransformer::SphericalVertexTransformer::tesselateTriangles(const unsigned int* indices, std::size_t count, VertexArray<cglib::vec2<float>>& coords, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<unsigned int>& tesselatedIndices) const {
        for (std::size_t i = 0; i + 2 < count; i += 3) {
            unsigned int i0 = indices[i + 0];
            unsigned int i1 = indices[i + 1];
            unsigned int i2 = indices[i + 2];
            float dist01 = cglib::length(coords[i1] - coords[i0]) * static_cast<float>(_tileScale);
            float dist02 = cglib::length(coords[i2] - coords[i0]) * static_cast<float>(_tileScale);
            float dist12 = cglib::length(coords[i2] - coords[i1]) * static_cast<float>(_tileScale);
            tesselateTriangle(i0, i1, i2, dist01, dist02, dist12, coords, texCoords, tesselatedIndices);
        }
    }

    cglib::vec2<double> SphericalTileTransformer::SphericalVertexTransformer::tileToEPSG3857(const cglib::vec2<float>& pos) const {
        return _tileOffset + cglib::vec2<double>(pos(0) * _tileScale, (1 - pos(1)) * _tileScale);
    }

    cglib::vec2<float> SphericalTileTransformer::SphericalVertexTransformer::epsg3857ToTile(const cglib::vec2<double>& epsg3857Pos) const {
        cglib::vec2<double> tilePosInv = (epsg3857Pos - _tileOffset) * (1.0 / _tileScale);
        return cglib::vec2<float>::convert(cglib::vec2<double>(tilePosInv(0), 1 - tilePosInv(1)));
    }

    cglib::vec3<double> SphericalTileTransformer::SphericalVertexTransformer::tileToSpherical(const cglib::vec2<float>& pos) const {
        if (_tileId.y < 0) {
            if (pos(1) <= 0) {
                return cglib::vec3<double>(0, 0, 1);
            }
        }
        else if (_tileId.y >= (1 << _tileId.zoom)) {
            if (pos(1) >= 1) {
                return cglib::vec3<double>(0, 0, -1);
            }
        }
        return epsg3857ToSpherical(tileToEPSG3857(pos));
    }
    
    cglib::vec2<float> SphericalTileTransformer::SphericalVertexTransformer::sphericalToTile(const cglib::vec3<double>& p) const {
        if (_tileId.y < 0) {
            if (p(2) == 1) {
                return cglib::vec2<float>(0, 0);
            }
        }
        else if (_tileId.y >= (1 << _tileId.zoom)) {
            if (p(2) == -1) {
                return cglib::vec2<float>(0, 1);
            }
        }
        return epsg3857ToTile(sphericalToEPSG3857(p));
    }

    void SphericalTileTransformer::SphericalVertexTransformer::tesselateSegment(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, float dist, VertexArray<cglib::vec2<float>>& points) const {
        if (dist > _divideThreshold) {
            cglib::vec2<float> posM = (pos0 + pos1) * 0.5f;
            tesselateSegment(pos0, posM, dist * 0.5f, points);
            tesselateSegment(posM, pos1, dist * 0.5f, points);
        }
        else {
            points.append(pos1);
        }
    }

    void SphericalTileTransformer::SphericalVertexTransformer::tesselateTriangle(unsigned int i0, unsigned int i1, unsigned int i2, float dist01, float dist02, float dist12, VertexArray<cglib::vec2<float>>& coords, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<unsigned int>& indices) const {
        if (dist01 > _divideThreshold || dist02 > _divideThreshold || dist12 > _divideThreshold) {
            unsigned int iM = static_cast<unsigned int>(coords.size());
            if (dist01 > dist02 && dist01 > dist12) {
                coords.append((coords[i0] + coords[i1]) * 0.5f);
                if (!texCoords.empty()) {
                    texCoords.append((texCoords[i0] + texCoords[i1]) * 0.5f);
                }
                float dist2M = cglib::length(coords[iM] - coords[i2]) * static_cast<float>(_tileScale);
                tesselateTriangle(i2, i0, iM, dist02, dist2M, dist01 * 0.5f, coords, texCoords, indices);
                tesselateTriangle(i1, i2, iM, dist12, dist01 * 0.5f, dist2M, coords, texCoords, indices);
            }
            else if (dist02 > dist12) {
                coords.append((coords[i0] + coords[i2]) * 0.5f);
                if (!texCoords.empty()) {
                    texCoords.append((texCoords[i0] + texCoords[i2]) * 0.5f);
                }
                float dist1M = cglib::length(coords[iM] - coords[i1]) * static_cast<float>(_tileScale);
                tesselateTriangle(i0, i1, iM, dist01, dist02 * 0.5f, dist1M, coords, texCoords, indices);
                tesselateTriangle(i1, i2, iM, dist12, dist1M, dist02 * 0.5f, coords, texCoords, indices);
            }
            else {
                coords.append((coords[i1] + coords[i2]) * 0.5f);
                if (!texCoords.empty()) {
                    texCoords.append((texCoords[i1] + texCoords[i2]) * 0.5f);
                }
                float dist0M = cglib::length(coords[iM] - coords[i0]) * static_cast<float>(_tileScale);
                tesselateTriangle(i0, i1, iM, dist01, dist0M, dist12 * 0.5f, coords, texCoords, indices);
                tesselateTriangle(i2, i0, iM, dist02, dist12 * 0.5f, dist0M, coords, texCoords, indices);
            }
        }
        else {
            indices.append(i0, i1, i2);
        }
    }

    cglib::vec3<double> SphericalTileTransformer::calculateTileOrigin(const TileId& tileId) const {
        cglib::vec2<double> epsg3857Pos = tileOffset(tileId);
        return epsg3857ToSpherical(epsg3857Pos) * _scale;
    }

    cglib::bbox3<double> SphericalTileTransformer::calculateTileBBox(const TileId& tileId) const {
        cglib::vec2<double> epsg3857Pos = tileOffset(tileId);
        cglib::bbox3<double> bbox = cglib::bbox3<double>::smallest();

        // Root tile?
        if (tileId == TileId(0, 0, 0)) {
            double rz = std::tanh(epsg3857Pos(1) / EARTH_RADIUS);
            bbox.add(cglib::vec3<double>(-1, -1, -rz) * _scale);
            bbox.add(cglib::vec3<double>( 1,  1,  rz) * _scale);
            return bbox;
        }
        
        // Add tile corners.
        double x0 = epsg3857Pos(0);
        double x1 = epsg3857Pos(0) + tileScale(tileId);
        double y0 = epsg3857Pos(1);
        double y1 = epsg3857Pos(1) + tileScale(tileId);
        bbox.add(epsg3857ToSpherical(cglib::vec2<double>(x0, y0)) * _scale);
        bbox.add(epsg3857ToSpherical(cglib::vec2<double>(x1, y0)) * _scale);
        bbox.add(epsg3857ToSpherical(cglib::vec2<double>(x0, y1)) * _scale);
        bbox.add(epsg3857ToSpherical(cglib::vec2<double>(x1, y1)) * _scale);

        // Check if tile crosses PI * N/4 boundaries. In that case we need to explicitly add such points.
        if (tileId.zoom < 2) {
            for (int i = -1; i <= 1; i++) {
                double x = EARTH_CIRCUMFERENCE * i / 4;
                if (x0 < x && x1 > x) {
                    bbox.add(epsg3857ToSpherical(cglib::vec2<double>(x, y0)) * _scale);
                    bbox.add(epsg3857ToSpherical(cglib::vec2<double>(x, y1)) * _scale);
                }
            }
        }
        
        // Add pole?
        if (tileId.y < 0 || tileId.y > (1 << tileId.zoom)) {
            bbox.add(cglib::vec3<double>(0, 0, tileId.y < 0 ? _scale : _scale));
        }

        return bbox;
    }

    cglib::mat4x4<double> SphericalTileTransformer::calculateTileMatrix(const TileId& tileId, float coordScale) const {
        double s = _scale * coordScale / (1 << tileId.zoom);
        cglib::vec3<double> p = calculateTileOrigin(tileId);

        cglib::mat4x4<double> m = cglib::mat4x4<double>::zero();
        m(0, 0) = s;
        m(1, 1) = s;
        m(2, 2) = s;
        m(0, 3) = p(0);
        m(1, 3) = p(1);
        m(2, 3) = p(2);
        m(3, 3) = 1;
        return m;
    }

    cglib::mat4x4<float> SphericalTileTransformer::calculateTileTransform(const TileId& tileId, const cglib::vec2<float>& translate, float coordScale) const {
        if (translate == cglib::vec2<float>(0, 0)) {
            return cglib::mat4x4<float>::identity();
        }

        // Find tile-specific rotation axis. This provides consistent look with planar case for higher zoom levels (> 3)
        SphericalVertexTransformer transformer(tileId, static_cast<float>(_scale), calculateTileOrigin(tileId), _divideThreshold);
        cglib::vec3<float> zAxis = transformer.calculateNormal(cglib::vec2<float>(0.5f, 0.5f));
        cglib::vec3<float> xAxis = transformer.calculateVector(cglib::vec2<float>(0.5f, 0.5f), cglib::vec2<float>(1.0f, 0.0f));
        cglib::vec3<float> yAxis = transformer.calculateVector(cglib::vec2<float>(0.5f, 0.5f), cglib::vec2<float>(0.0f, 1.0f));
        cglib::mat4x4<float> rotateMatrixX = cglib::rotate4_matrix(cglib::vector_product(zAxis, xAxis),  translate(0) * cglib::length(xAxis) / (1 << tileId.zoom));
        cglib::mat4x4<float> rotateMatrixY = cglib::rotate4_matrix(cglib::vector_product(yAxis, zAxis), -translate(1) * cglib::length(yAxis) / (1 << tileId.zoom));
        cglib::mat4x4<double> tileMatrix = calculateTileMatrix(tileId, coordScale);
        return cglib::mat4x4<float>::convert(cglib::inverse(tileMatrix) * cglib::mat4x4<double>::convert(rotateMatrixY * rotateMatrixX) * tileMatrix);
    }

    std::shared_ptr<const TileTransformer::VertexTransformer> SphericalTileTransformer::createTileVertexTransformer(const TileId& tileId) const {
        return std::make_shared<SphericalVertexTransformer>(tileId, static_cast<float>(_scale), calculateTileOrigin(tileId), _divideThreshold);
    }

    cglib::vec2<double> SphericalTileTransformer::tileOffset(const TileId& tileId) {
        int tileMask = (1 << tileId.zoom) - 1;
        double zoomScale = 1.0 / (1 << tileId.zoom);
        double x = ((tileId.x & tileMask) * zoomScale - 0.5) * EARTH_CIRCUMFERENCE;
        double y = ((tileMask - tileId.y) * zoomScale - 0.5) * EARTH_CIRCUMFERENCE;
        return cglib::vec2<double>(x, y);
    }

    double SphericalTileTransformer::tileScale(const TileId& tileId) {
        return EARTH_CIRCUMFERENCE / (1 << tileId.zoom);
    }

    cglib::vec3<double> SphericalTileTransformer::epsg3857ToSpherical(const cglib::vec2<double>& epsg3857Pos) {
        double x1 = epsg3857Pos(0) / EARTH_RADIUS;
        double y1 = epsg3857Pos(1) / EARTH_RADIUS;
        double rz = std::tanh(y1);
        double ss = std::sqrt(std::max(0.0, 1.0 - rz * rz));
        double rx = ss * std::cos(x1);
        double ry = ss * std::sin(x1);
        return cglib::vec3<double>(rx, ry, rz);
    }

    cglib::vec2<double> SphericalTileTransformer::sphericalToEPSG3857(const cglib::vec3<double>& p) {
        double x1 = p(0) != 0 || p(1) != 0 ? std::atan2(p(1), p(0)) : 0;
        double y1 = std::atanh(std::max(-1.0, std::min(1.0, p(2) / cglib::length(p))));
        return cglib::vec2<double>(x1 * EARTH_RADIUS, y1 * EARTH_RADIUS);
    }
} }

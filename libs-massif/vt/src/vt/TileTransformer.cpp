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

    void DefaultTileTransformer::DefaultVertexTransformer::tesselateLineString(const std::vector<cglib::vec2<float>>& linePoints, VertexArray<cglib::vec2<float>>& points) const {
        points.reserve(linePoints.size());
        for (const cglib::vec2<float>& pos : linePoints) {
            points.append(pos);
        }
    }

    void DefaultTileTransformer::DefaultVertexTransformer::tesselateTriangle(unsigned int i0, unsigned int i1, unsigned int i2, VertexArray<cglib::vec2<float>>& coords, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<unsigned int>& indices) const {
        indices.append(i0, i1, i2);
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

    cglib::vec3<double> DefaultTileTransformer::calculateTileNormal(const TileId& tileId, double& maxAngle) const {
        maxAngle = 0.0;
        return cglib::vec3<double>(0, 0, 1);
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

    boost::optional<cglib::vec3<double>> DefaultTileTransformer::calculatePoleOrigin(int poleZ) const {
        return boost::optional<cglib::vec3<double>>();
    }

    boost::optional<cglib::vec3<double>> DefaultTileTransformer::calculatePoleNormal(int poleZ) const {
        return boost::optional<cglib::vec3<double>>();
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
            cglib::vec3<double> p = epsg3857ToSpherical(tileToEPSG3857(pos));
            return cglib::vec3<float>::convert((p - _origin) * static_cast<double>(1 << _tileId.zoom));
        }
        
        // Hack: for points at tile borders (or crossing them) discretesize tile borders with triangles. This avoids cracks between tiles.
        int gridSize = std::max(1, ZOOM_0_GRID_SIZE / (1 << _tileId.zoom));
        float x = std::floor(pos(0) * gridSize);
        float y = std::floor(pos(1) * gridSize);
        double u = pos(0) * gridSize - x;
        double v = pos(1) * gridSize - y;
        cglib::vec3<double> p00 = epsg3857ToSpherical(tileToEPSG3857(cglib::vec2<float>((x + 0) / gridSize, (y + 0) / gridSize)));
        cglib::vec3<double> p11 = epsg3857ToSpherical(tileToEPSG3857(cglib::vec2<float>((x + 1) / gridSize, (y + 1) / gridSize)));
        cglib::vec3<double> du;
        cglib::vec3<double> dv;
        if (u > v) {
            cglib::vec3<double> p10 = epsg3857ToSpherical(tileToEPSG3857(cglib::vec2<float>((x + 1) / gridSize, (y + 0) / gridSize)));
            du = p10 - p00;
            dv = p11 - p10;
        }
        else {
            cglib::vec3<double> p01 = epsg3857ToSpherical(tileToEPSG3857(cglib::vec2<float>((x + 0) / gridSize, (y + 1) / gridSize)));
            dv = p01 - p00;
            du = p11 - p01;
        }
        return cglib::vec3<float>::convert((p00 + du * u + dv * v - _origin) * static_cast<double>(1 << _tileId.zoom));
    }

    cglib::vec3<float> SphericalTileTransformer::SphericalVertexTransformer::calculateNormal(const cglib::vec2<float>& pos) const {
        cglib::vec2<double> epsg3857Pos = tileToEPSG3857(pos);
        return cglib::vec3<float>::convert(epsg3857ToSpherical(epsg3857Pos));
    }

    cglib::vec3<float> SphericalTileTransformer::SphericalVertexTransformer::calculateVector(const cglib::vec2<float>& pos, const cglib::vec2<float>& vec) const {
        cglib::vec2<double> epsg3857Pos = tileToEPSG3857(pos);
        cglib::vec3<double> xyzPos = epsg3857ToSpherical(epsg3857Pos);

        float x = static_cast<float>(xyzPos(0));
        float y = static_cast<float>(xyzPos(1));
        float z = static_cast<float>(xyzPos(2));
        
        float scale = static_cast<float>(PI) / std::sqrt(x * x + y * y);

        float dx_du = -y;
        float dy_du = x;
        float dz_du = 0;

        float dx_dv = z * x;
        float dy_dv = z * y;
        float dz_dv = -(x * x + y * y);

        cglib::vec3<float> xyzVec;
        xyzVec(0) = (dx_du * vec(0) + dx_dv * vec(1)) * scale;
        xyzVec(1) = (dy_du * vec(0) + dy_dv * vec(1)) * scale;
        xyzVec(2) = (dz_du * vec(0) + dz_dv * vec(1)) * scale;
        return xyzVec;
    }

    cglib::vec2<float> SphericalTileTransformer::SphericalVertexTransformer::calculateTilePosition(const cglib::vec3<float>& pos) const {
        cglib::vec3<double> sphericalPos = cglib::vec3<double>::convert(pos) * (1.0 / (1 << _tileId.zoom)) + _origin;
        cglib::vec2<double> epsg3857Pos = sphericalToEPSG3857(sphericalPos);
        return epsg3857ToTile(epsg3857Pos);
    }

    float SphericalTileTransformer::SphericalVertexTransformer::calculateHeight(const cglib::vec2<float>& pos, float height) const {
        return static_cast<float>(height * (1 << _tileId.zoom) / EARTH_CIRCUMFERENCE * 2 * PI);
    }

    void SphericalTileTransformer::SphericalVertexTransformer::tesselateLineString(const std::vector<cglib::vec2<float>>& linePoints, VertexArray<cglib::vec2<float>>& points) const {
        if (linePoints.empty()) {
            return;
        }

        points.append(linePoints[0]);
        for (std::size_t i = 1; i < linePoints.size(); i++) {
            float dist = cglib::length(linePoints[i] - linePoints[i - 1]) * static_cast<float>(_tileScale);
            tesselateSegment(linePoints[i - 1], linePoints[i], dist, points);
        }
    }

    void SphericalTileTransformer::SphericalVertexTransformer::tesselateTriangle(unsigned int i0, unsigned int i1, unsigned int i2, VertexArray<cglib::vec2<float>>& coords, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<unsigned int>& indices) const {
        float dist01 = cglib::length(coords[i1] - coords[i0]) * static_cast<float>(_tileScale);
        float dist02 = cglib::length(coords[i2] - coords[i0]) * static_cast<float>(_tileScale);
        float dist12 = cglib::length(coords[i2] - coords[i1]) * static_cast<float>(_tileScale);
        tesselateTriangle(i0, i1, i2, dist01, dist02, dist12, coords, texCoords, indices);
    }

    cglib::vec2<double> SphericalTileTransformer::SphericalVertexTransformer::tileToEPSG3857(const cglib::vec2<float>& pos) const {
        return _tileOffset + cglib::vec2<double>(pos(0) * _tileScale, (1 - pos(1)) * _tileScale);
    }

    cglib::vec2<float> SphericalTileTransformer::SphericalVertexTransformer::epsg3857ToTile(const cglib::vec2<double>& epsg3857Pos) const {
        cglib::vec2<double> tilePosInv = (epsg3857Pos - _tileOffset) * (1.0 / _tileScale);
        return cglib::vec2<float>::convert(cglib::vec2<double>(tilePosInv(0), 1 - tilePosInv(1)));
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
        int n = 4 - std::min(tileId.zoom, 2);
        cglib::vec2<double> epsg3857Pos = tileOffset(tileId);
        cglib::bbox3<double> bbox = cglib::bbox3<double>::smallest();
        for (int i = 0; i <= n; i++) {
            for (int j = 0; j <= n; j++) {
                double x = epsg3857Pos(0) + tileScale(tileId) * i / n;
                double y = epsg3857Pos(1) + tileScale(tileId) * j / n;
                bbox.add(epsg3857ToSpherical(cglib::vec2<double>(x, y)) * _scale);
            }
        }
        return bbox;
    }

    cglib::vec3<double> SphericalTileTransformer::calculateTileNormal(const TileId& tileId, double& maxAngle) const {
        if (tileId.zoom == 0) {
            maxAngle = PI;
            return cglib::vec3<double>(0, 0, 1);
        }

        cglib::vec2<double> epsg3857Pos00 = tileOffset(tileId);
        cglib::vec2<double> epsg3857Pos11 = tileOffset(TileId(tileId.zoom, tileId.x + 1, tileId.y - 1));
        cglib::vec3<double> p00 = epsg3857ToSpherical(epsg3857Pos00);
        cglib::vec3<double> p11 = epsg3857ToSpherical(epsg3857Pos11);
        cglib::vec3<double> pCC = cglib::unit(p00 + p11);
        maxAngle = tileId.zoom > 1 ? std::acos(std::min(1.0, std::max(-1.0, cglib::dot_product(p00, pCC)))) : PI / 2;
        return pCC;
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

    boost::optional<cglib::vec3<double>> SphericalTileTransformer::calculatePoleOrigin(int poleZ) const {
        return cglib::vec3<double>(0, 0, -poleZ * _scale);
    }

    boost::optional<cglib::vec3<double>> SphericalTileTransformer::calculatePoleNormal(int poleZ) const {
        return cglib::vec3<double>(0, 0, -poleZ);
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

    cglib::vec2<double> SphericalTileTransformer::sphericalToEPSG3857(const cglib::vec3<double>& pos) {
        cglib::vec3<double> unitPos = cglib::unit(pos);
        double x1 = pos(0) != 0 || pos(1) != 0 ? std::atan2(pos(1), pos(0)) : 0;
        double y1 = std::atanh(std::max(-1.0, std::min(1.0, unitPos(2))));
        return cglib::vec2<double>(x1 * EARTH_RADIUS, y1 * EARTH_RADIUS);
    }
} }

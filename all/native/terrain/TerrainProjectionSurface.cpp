#include "TerrainProjectionSurface.h"
#include "datasources/TileDataSource.h"
#include "terrain/ElevationManager.h"
#include "utils/Const.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace massif {

    TerrainProjectionSurface::TerrainProjectionSurface(const std::shared_ptr<ElevationManager>& elevationManager) :
        PlanarProjectionSurface(),
        _elevationManager(elevationManager),
        _elevationVersion(elevationManager->getVersion()),
        _splitThreshold(CalculateSplitThreshold(elevationManager)),
        _heightLift(CalculateSplitThreshold(elevationManager) * 0.2)
    {
    }

    MapPos TerrainProjectionSurface::calculateMapPos(const cglib::vec3<double>& pos) const {
        double terrainZ = _elevationManager->getDisplayHeight(pos(0), pos(1), ElevationManager::LoadMode::CACHED_ONLY);
        return MapPos(pos(0), pos(1), pos(2) - terrainZ - _heightLift);
    }

    cglib::vec3<double> TerrainProjectionSurface::calculatePosition(const MapPos& mapPos) const {
        // Cached-only: element positioning may run on the UI thread and must never block on IO;
        // MapRenderer rebuilds the draw data when the elevation version changes. The small lift
        // keeps densely sampled draped geometry clear of the terrain depth in concave areas.
        double terrainZ = _elevationManager->getDisplayHeight(mapPos.getX(), mapPos.getY(), ElevationManager::LoadMode::CACHED_ONLY);
        return cglib::vec3<double>(mapPos.getX(), mapPos.getY(), mapPos.getZ() + terrainZ + _heightLift);
    }

    cglib::vec3<double> TerrainProjectionSurface::calculateNormal(const MapPos& mapPos) const {
        // The terrain surface normal (from the elevation gradient). Line width extrusion
        // is performed perpendicular to this normal, so wide lines lie in the local
        // terrain tangent plane instead of a horizontal plane cutting into slopes.
        double dhdx = 0, dhdy = 0;
        _elevationManager->getDisplayGradient(mapPos.getX(), mapPos.getY(), ElevationManager::LoadMode::CACHED_ONLY, dhdx, dhdy);
        return cglib::unit(cglib::vec3<double>(-dhdx, -dhdy, 1));
    }

    cglib::vec3<double> TerrainProjectionSurface::calculateVector(const MapPos& mapPos, const MapVec& mapVec) const {
        // Tilt local vectors into the terrain tangent plane (see calculateNormal)
        double dhdx = 0, dhdy = 0;
        _elevationManager->getDisplayGradient(mapPos.getX(), mapPos.getY(), ElevationManager::LoadMode::CACHED_ONLY, dhdx, dhdy);
        return cglib::vec3<double>(mapVec.getX(), mapVec.getY(), mapVec.getZ() + dhdx * mapVec.getX() + dhdy * mapVec.getY());
    }

    cglib::vec3<double> TerrainProjectionSurface::calculateNearestPoint(const cglib::vec3<double>& pos, double height) const {
        double terrainZ = _elevationManager->getDisplayHeight(pos(0), pos(1), ElevationManager::LoadMode::CACHED_ONLY);
        return cglib::vec3<double>(pos(0), pos(1), height + terrainZ + _heightLift);
    }

    bool TerrainProjectionSurface::calculateHitPoint(const cglib::ray3<double>& ray, double height, double& t) const {
        if (_elevationManager->intersectRay(ray, t)) {
            return true;
        }
        return PlanarProjectionSurface::calculateHitPoint(ray, height, t);
    }

    void TerrainProjectionSurface::tesselateSegment(const MapPos& mapPos0, const MapPos& mapPos1, std::vector<MapPos>& mapPoses) const {
        // Subdivide long segments so that draped lines follow the terrain surface instead
        // of cutting straight through it (heights are applied per vertex in calculatePosition).
        // The point count per input segment is bounded to keep degenerate inputs cheap.
        double dx = mapPos1.getX() - mapPos0.getX();
        double dy = mapPos1.getY() - mapPos0.getY();
        double len = std::sqrt(dx * dx + dy * dy);
        int count = 1;
        if (_splitThreshold > 0 && std::isfinite(len)) {
            count = std::min(512, std::max(1, static_cast<int>(std::ceil(len / _splitThreshold))));
        }
        mapPoses.push_back(mapPos0);
        for (int i = 1; i < count; i++) {
            double t = static_cast<double>(i) / count;
            mapPoses.push_back(MapPos(mapPos0.getX() + dx * t, mapPos0.getY() + dy * t, mapPos0.getZ() + (mapPos1.getZ() - mapPos0.getZ()) * t));
        }
        mapPoses.push_back(mapPos1);
    }

    void TerrainProjectionSurface::tesselateTriangle(unsigned int i0, unsigned int i1, unsigned int i2, std::vector<unsigned int>& indices, std::vector<MapPos>& mapPoses) const {
        // Iterative longest-edge bisection with a bounded output budget per input triangle
        int budget = 2048;
        std::vector<std::array<unsigned int, 3> > stack;
        stack.push_back({ { i0, i1, i2 } });
        while (!stack.empty()) {
            std::array<unsigned int, 3> tri = stack.back();
            stack.pop_back();

            MapPos mapPos0 = mapPoses.at(tri[0]);
            MapPos mapPos1 = mapPoses.at(tri[1]);
            MapPos mapPos2 = mapPoses.at(tri[2]);

            MapPos mapPosM;
            if (budget > 0 && splitSegment(mapPos0, mapPos1, mapPosM)) {
                unsigned int iM = static_cast<unsigned int>(mapPoses.size());
                mapPoses.push_back(mapPosM);
                stack.push_back({ { tri[2], tri[0], iM } });
                stack.push_back({ { tri[1], tri[2], iM } });
            } else if (budget > 0 && splitSegment(mapPos0, mapPos2, mapPosM)) {
                unsigned int iM = static_cast<unsigned int>(mapPoses.size());
                mapPoses.push_back(mapPosM);
                stack.push_back({ { tri[0], tri[1], iM } });
                stack.push_back({ { tri[1], tri[2], iM } });
            } else if (budget > 0 && splitSegment(mapPos1, mapPos2, mapPosM)) {
                unsigned int iM = static_cast<unsigned int>(mapPoses.size());
                mapPoses.push_back(mapPosM);
                stack.push_back({ { tri[0], tri[1], iM } });
                stack.push_back({ { tri[2], tri[0], iM } });
            } else {
                indices.push_back(tri[0]);
                indices.push_back(tri[1]);
                indices.push_back(tri[2]);
            }
            budget--;
        }
    }

    bool TerrainProjectionSurface::splitSegment(const MapPos& mapPos0, const MapPos& mapPos1, MapPos& mapPosM) const {
        double dx = mapPos1.getX() - mapPos0.getX();
        double dy = mapPos1.getY() - mapPos0.getY();
        if (dx * dx + dy * dy <= _splitThreshold * _splitThreshold) {
            return false;
        }
        mapPosM = MapPos((mapPos0.getX() + mapPos1.getX()) * 0.5, (mapPos0.getY() + mapPos1.getY()) * 0.5, (mapPos0.getZ() + mapPos1.getZ()) * 0.5);
        return true;
    }

    double TerrainProjectionSurface::CalculateSplitThreshold(const std::shared_ptr<ElevationManager>& elevationManager) {
        // Subdivide down to roughly the elevation data texel size (assuming 256px tiles at the
        // maximum data source zoom level), clamped to a sane range to bound vertex counts.
        int maxZoom = 12;
        if (std::shared_ptr<TileDataSource> dataSource = elevationManager->getDataSource()) {
            maxZoom = std::min(20, std::max(0, dataSource->getMaxZoom()));
        }
        double texelSize = Const::WORLD_SIZE / (static_cast<double>(1 << maxZoom) * 256.0);
        return std::max(texelSize, Const::WORLD_SIZE / static_cast<double>(1 << 22));
    }
}

#include "SunExposureJob.h"

#include "components/SunExposureOptions.h"
#include "datasources/TileDataSource.h"
#include "projections/Projection.h"
#include "terrain/ElevationManager.h"
#include "terrain/ElevationTileGrid.h"
#include "terrain/SunExposure.h"
#include "utils/Const.h"
#include "utils/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace massif {

    namespace {
        constexpr int LOAD_THREADS = 3; // the elevation manager's own prefetch worker count
        constexpr int MAX_ANCESTOR_DEPTH = 8; // as the manager's cached ancestor walk
        constexpr int MAX_COARSE_TILES = 9;
        constexpr double COARSE_RELIEF_MARGIN = 0.15; // coarse grids are averaged: their peaks sit low
        constexpr double COARSE_HEIGHT_MARGIN = 100.0;
        constexpr double GROUND_ESTIMATE_SLACK = 150.0; // under the coarse ground, so the planned march is never short
        constexpr double NO_REGION_MAX = 9000.0;
        constexpr double NO_REGION_MIN = -500.0;

        using Clock = std::chrono::steady_clock;

        /** The grids one call holds, by the tile the march asked for (a grid may be a coarser stand-in; null = none came). */
        struct Grids {
            std::unordered_map<long long, std::shared_ptr<ElevationTileGrid> > byTile;
            std::size_t bytes = 0;
            int texelsPerTile = ElevationManager::DEM_TEXELS_PER_TILE_UNIT;
        };

        struct Geometry {
            int minZoom = 0;
            int maxZoom = 0;
        };

        MapTile tileAt(double x, double y, int zoom) {
            long long n = 1LL << zoom;
            double u = x / Const::WORLD_SIZE + 0.5;
            double v = 0.5 - y / Const::WORLD_SIZE; // XYZ: tile y grows south
            long long tx = static_cast<long long>(std::floor(u * n)) % n;
            if (tx < 0) {
                tx += n;
            }
            long long ty = std::max(0LL, std::min(n - 1, static_cast<long long>(std::floor(v * n))));
            return MapTile(static_cast<int>(tx), static_cast<int>(ty), zoom, 0);
        }

        int zoomFor(double texel, double worldMeters, int texelsPerTile, const Geometry& geometry) {
            double zoom = std::log2(worldMeters / (texelsPerTile * texel));
            return std::max(geometry.minZoom, std::min(geometry.maxZoom, static_cast<int>(std::lround(zoom))));
        }

        /** The ground along one point's ray, read from the grids this call holds. */
        class RayGround {
        public:
            RayGround(const Grids& grids, const Geometry& geometry) : _grids(grids), _geometry(geometry) { }

            void setOrigin(double x, double y, double metersToInternal, double worldMeters) {
                _x = x;
                _y = y;
                _scale = metersToInternal;
                _worldMeters = worldMeters;
            }

            int getMinZoomUsed() const { return _minZoomUsed; }
            int getMaxZoomUsed() const { return _maxZoomUsed; }

            bool operator()(double east, double north, double texel, double dirEast, double dirNorth, SunExposure::GroundSample& out) {
                int zoom = zoomFor(texel, _worldMeters, _grids.texelsPerTile, _geometry);
                double x = _x + east * _scale, y = _y + north * _scale;
                const ElevationTileGrid* grid = find(x, y, zoom);
                if (!grid) {
                    return false;
                }
                int gridZoom = grid->getTile().getZoom();
                _minZoomUsed = std::min(_minZoomUsed, gridZoom);
                _maxZoomUsed = std::max(_maxZoomUsed, gridZoom);
                out.height = grid->sampleHeight(x, y);
                out.tileMax = grid->getMaxHeight();
                out.coarse = zoom - gridZoom > 2;
                const MapBounds& bounds = grid->getInternalBounds();
                double exitX = dirEast > 0 ? (bounds.getMax().getX() - x) / (dirEast * _scale) : (dirEast < 0 ? (bounds.getMin().getX() - x) / (dirEast * _scale) : EXIT_NEVER);
                double exitY = dirNorth > 0 ? (bounds.getMax().getY() - y) / (dirNorth * _scale) : (dirNorth < 0 ? (bounds.getMin().getY() - y) / (dirNorth * _scale) : EXIT_NEVER);
                out.exitDistance = std::max(0.0, std::min(EXIT_NEVER, std::min(exitX, exitY)));
                return true;
            }

        private:
            static constexpr double EXIT_NEVER = 1e9;

            const ElevationTileGrid* find(double x, double y, int zoom) {
                MapTile wanted = tileAt(x, y, zoom);
                long long wantedId = wanted.getTileId();
                if (wantedId == _memoId) {
                    return _memoGrid;
                }
                const ElevationTileGrid* found = nullptr;
                MapTile tile = wanted;
                for (int depth = 0; depth <= MAX_ANCESTOR_DEPTH; depth++) {
                    auto it = _grids.byTile.find(tile.getTileId());
                    if (it != _grids.byTile.end() && it->second) {
                        found = it->second.get();
                        break;
                    }
                    if (tile.getZoom() <= _geometry.minZoom) {
                        break;
                    }
                    tile = tile.getParent();
                }
                _memoId = wantedId;
                _memoGrid = found;
                return found;
            }

            const Grids& _grids;
            const Geometry& _geometry;
            double _x = 0, _y = 0, _scale = 1, _worldMeters = 1;
            long long _memoId = -1;
            const ElevationTileGrid* _memoGrid = nullptr;
            int _minZoomUsed = std::numeric_limits<int>::max();
            int _maxZoomUsed = -1;
        };

        struct LoadReport {
            bool deadlineHit = false;
        };

        /**
         * Gets the grids of `tiles` into `grids`, in order, on a few threads. Loading tiles go through the
         * manager's own LOAD_EXACT, so a tile in flight elsewhere is shared and a failure is remembered.
         */
        void loadTiles(ElevationManager& manager, const std::vector<MapTile>& tiles, bool load, Clock::time_point deadline, bool hasDeadline, std::size_t budgetBytes, Grids& grids, LoadReport& report) {
            std::vector<MapTile> todo;
            for (const MapTile& tile : tiles) {
                if (grids.byTile.find(tile.getTileId()) == grids.byTile.end()) {
                    todo.push_back(tile);
                    grids.byTile[tile.getTileId()] = nullptr;
                }
            }
            std::mutex mutex;
            std::atomic<std::size_t> next { 0 };
            std::atomic<bool> deadlineHit { false };
            auto work = [&]() {
                while (true) {
                    std::size_t index = next.fetch_add(1);
                    if (index >= todo.size()) {
                        return;
                    }
                    const MapTile& tile = todo[index];
                    if (!load) {
                        std::shared_ptr<ElevationTileGrid> grid = manager.getDataTileGrid(tile, ElevationManager::LoadMode::CACHED_EXACT);
                        if (!grid) {
                            manager.requestTileGrid(tile, 0);
                            continue;
                        }
                        std::lock_guard<std::mutex> lock(mutex);
                        grids.byTile[tile.getTileId()] = grid;
                        grids.bytes += grid->getDataSize();
                        continue;
                    }
                    if (hasDeadline && Clock::now() >= deadline) {
                        deadlineHit = true;
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (grids.bytes >= budgetBytes) {
                            return;
                        }
                    }
                    std::shared_ptr<ElevationTileGrid> grid;
                    try {
                        grid = manager.getDataTileGrid(tile, ElevationManager::LoadMode::LOAD_EXACT);
                    } catch (const std::exception& ex) {
                        Log::Warnf("SunExposureJob: elevation tile failed to load: %s", ex.what());
                    }
                    if (grid) {
                        std::lock_guard<std::mutex> lock(mutex);
                        grids.byTile[tile.getTileId()] = grid;
                        grids.bytes += grid->getDataSize();
                    }
                }
            };
            int threads = load ? std::min<int>(LOAD_THREADS, static_cast<int>(todo.size())) : 1;
            std::vector<std::thread> pool;
            for (int i = 1; i < threads; i++) {
                pool.emplace_back(work);
            }
            if (!todo.empty()) {
                work();
            }
            for (std::thread& thread : pool) {
                thread.join();
            }
            report.deadlineHit = report.deadlineHit || deadlineHit.load();
        }

        SunExposure::Params toParams(const SunExposureOptions& options) {
            SunExposure::Params params;
            params.eyeHeight = options.getEyeHeight();
            params.maxDistance = options.getMaxDistance();
            params.discRadius = options.getDiscRadius();
            params.darkBelow = options.getDarkBelow();
            params.lowSunBelow = options.getLowSunBelow();
            params.firstStep = options.getFirstStep();
            params.stepGrowth = options.getStepGrowth();
            params.lodRatio = options.getLodRatio();
            params.minTexel = options.getMinTexel();
            params.refraction = options.getRefraction();
            params.earthRadius = Const::EARTH_RADIUS;
            return params;
        }

        struct PointInput {
            double x = 0, y = 0;      // internal
            double scale = 0;         // internal units per metre at y
            double worldMeters = 0;   // ground metres in one world width at this latitude
            double azimuth = 0, altitude = 0;
            bool valid = false;
        };

        /** The tiles covering the points' region at one coarse level, for the region's highest and lowest ground. */
        std::vector<MapTile> coarseTiles(const std::vector<PointInput>& points, const SunExposure::Params& params, const Geometry& geometry, int& zoomOut) {
            double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18, worldMeters = 0;
            int count = 0;
            for (const PointInput& point : points) {
                if (!point.valid || point.altitude < params.darkBelow) {
                    continue;
                }
                double pointReach = params.maxDistance * point.scale;
                minX = std::min(minX, point.x - pointReach);
                maxX = std::max(maxX, point.x + pointReach);
                minY = std::min(minY, point.y - pointReach);
                maxY = std::max(maxY, point.y + pointReach);
                worldMeters += point.worldMeters;
                count++;
            }
            std::vector<MapTile> tiles;
            if (count == 0) {
                return tiles;
            }
            worldMeters /= count;
            int zoom = std::max(geometry.minZoom, std::min(geometry.maxZoom, static_cast<int>(std::floor(std::log2(worldMeters / params.maxDistance)))));
            while (true) {
                MapTile low = tileAt(minX, maxY, zoom), high = tileAt(maxX, minY, zoom);
                int across = high.getX() - low.getX() + 1, down = high.getY() - low.getY() + 1;
                if (across < 1) {
                    across = 1; // wrapped over the antimeridian: one tile each side is the best this does
                }
                if (across * down <= MAX_COARSE_TILES || zoom <= geometry.minZoom) {
                    for (int ty = low.getY(); ty <= high.getY(); ty++) {
                        for (int i = 0; i < across; i++) {
                            tiles.push_back(MapTile((low.getX() + i) % (1 << zoom), ty, zoom, 0));
                        }
                    }
                    break;
                }
                zoom--;
            }
            zoomOut = zoom;
            return tiles;
        }
    }

    std::vector<double> SunExposureJob::compute(ElevationManager& manager, const std::vector<MapPos>& poses, const std::vector<double>& sunDirections, const std::shared_ptr<SunExposureOptions>& optionsPtr) {
        if (sunDirections.size() != poses.size() * 2) {
            throw std::invalid_argument("SunExposureJob::compute: sunDirections needs an azimuth and an altitude per position");
        }
        SunExposureOptions defaults;
        const SunExposureOptions& options = optionsPtr ? *optionsPtr : defaults;
        SunExposure::Params params = toParams(options);
        std::size_t count = poses.size();

        std::vector<double> result(HEADER_SIZE + count * POINT_STRIDE, 0.0);
        for (std::size_t i = 0; i < count; i++) {
            double* out = &result[HEADER_SIZE + i * POINT_STRIDE];
            out[0] = -1;
            out[1] = -90;
            out[2] = -1;
        }
        int flags = 0, minZoomUsed = std::numeric_limits<int>::max(), maxZoomUsed = -1;
        auto writeHeader = [&](int loaded, int missing) {
            result[0] = RESULT_VERSION;
            result[1] = static_cast<double>(count);
            result[2] = POINT_STRIDE;
            result[3] = loaded;
            result[4] = missing;
            result[5] = maxZoomUsed < 0 ? -1 : minZoomUsed;
            result[6] = maxZoomUsed;
            result[7] = flags;
        };

        std::shared_ptr<TileDataSource> dataSource = manager.getDataSource();
        if (!dataSource || count == 0) {
            writeHeader(0, 0);
            return result;
        }
        std::shared_ptr<Projection> projection = dataSource->getProjection();
        Geometry geometry;
        geometry.minZoom = dataSource->getMinZoom();
        geometry.maxZoom = manager.getMaxDataZoom();

        std::vector<PointInput> points(count);
        for (std::size_t i = 0; i < count; i++) {
            PointInput& point = points[i];
            point.azimuth = sunDirections[2 * i];
            point.altitude = sunDirections[2 * i + 1];
            double lon = poses[i].getX(), lat = poses[i].getY();
            if (!std::isfinite(lon) || !std::isfinite(lat) || !std::isfinite(point.azimuth) || !std::isfinite(point.altitude) || std::fabs(lat) > 85.0) {
                continue;
            }
            MapPos internal = projection->toInternal(projection->fromWgs84(poses[i]));
            point.x = internal.getX();
            point.y = internal.getY();
            point.scale = manager.getDisplayScale(point.y);
            point.worldMeters = Const::EARTH_CIRCUMFERENCE * std::cos(lat * SunExposure::DEG_TO_RAD);
            point.valid = point.scale > 0;
        }

        Grids grids;
        LoadReport report;
        Clock::time_point started = Clock::now();
        auto elapsedMs = [&]() { return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count()); };
        Clock::time_point deadline = started + std::chrono::milliseconds(options.getLoadTimeoutMs());
        bool hasDeadline = options.getLoadTimeoutMs() > 0;
        std::size_t budgetBytes = static_cast<std::size_t>(options.getCacheMB()) * 1024 * 1024;
        bool load = options.isLoadMissingTiles();
        std::unordered_set<long long> requested;

        // Round one: the coarse cover gives the highest ground, a ground estimate per point and the texels per tile.
        int coarseZoom = geometry.minZoom;
        std::vector<MapTile> coarse = coarseTiles(points, params, geometry, coarseZoom);
        for (const MapTile& tile : coarse) {
            requested.insert(tile.getTileId());
        }
        loadTiles(manager, coarse, load, deadline, hasDeadline, budgetBytes, grids, report);
        Log::Infof("SunExposureJob: %d points, coarse cover z%d: %d tiles asked, %d MB held, %d ms", static_cast<int>(count), coarseZoom, static_cast<int>(coarse.size()), static_cast<int>(grids.bytes >> 20), elapsedMs());
        double regionMax = std::numeric_limits<double>::lowest(), regionMin = std::numeric_limits<double>::max();
        for (const auto& entry : grids.byTile) {
            if (entry.second) {
                grids.texelsPerTile = entry.second->getWidth();
                regionMax = std::max(regionMax, static_cast<double>(entry.second->getMaxHeight()));
                regionMin = std::min(regionMin, static_cast<double>(entry.second->getMinHeight()));
            }
        }
        if (regionMax < regionMin) {
            regionMax = NO_REGION_MAX;
            regionMin = NO_REGION_MIN;
        } else {
            regionMax += COARSE_RELIEF_MARGIN * (regionMax - regionMin) + COARSE_HEIGHT_MARGIN;
        }

        // Round two: every tile a march can touch, in route order, so a budget or deadline costs the tail.
        std::vector<MapTile> planned;
        for (const PointInput& point : points) {
            if (!point.valid || point.altitude < params.darkBelow) {
                continue;
            }
            double groundEstimate = regionMin;
            auto it = grids.byTile.find(tileAt(point.x, point.y, coarseZoom).getTileId());
            if (it != grids.byTile.end() && it->second) {
                groundEstimate = it->second->sampleHeight(point.x, point.y);
            }
            groundEstimate -= GROUND_ESTIMATE_SLACK;
            long long lastId = -1;
            SunExposure::plan(point.azimuth, point.altitude, params, groundEstimate, regionMax, [&](double east, double north, double texel) {
                MapTile tile = tileAt(point.x + east * point.scale, point.y + north * point.scale, zoomFor(texel, point.worldMeters, grids.texelsPerTile, geometry));
                long long id = tile.getTileId();
                if (id != lastId && requested.insert(id).second) {
                    planned.push_back(tile);
                }
                lastId = id;
            });
        }
        loadTiles(manager, planned, load, deadline, hasDeadline, budgetBytes, grids, report);
        Log::Infof("SunExposureJob: %d marched tiles planned, %d MB held, deadline hit %d, %d ms", static_cast<int>(planned.size()), static_cast<int>(grids.bytes >> 20), report.deadlineHit ? 1 : 0, elapsedMs());

        // Round three: the marches, on the grids held here whatever the manager's cache evicts.
        RayGround ground(grids, geometry);
        for (std::size_t i = 0; i < count; i++) {
            const PointInput& point = points[i];
            double* out = &result[HEADER_SIZE + i * POINT_STRIDE];
            if (!point.valid) {
                out[3] = SunExposure::NO_DEM;
                continue;
            }
            ground.setOrigin(point.x, point.y, point.scale, point.worldMeters);
            SunExposure::Point exposure = SunExposure::exposure(std::ref(ground), point.azimuth, point.altitude, params, regionMax);
            out[0] = exposure.sunFraction;
            out[1] = exposure.horizon;
            out[2] = exposure.shadowDistance;
            out[3] = exposure.status;
            if (exposure.status & SunExposure::TRUNCATED) {
                flags |= FLAG_TRUNCATED;
            }
        }
        minZoomUsed = ground.getMinZoomUsed();
        maxZoomUsed = ground.getMaxZoomUsed();
        if (report.deadlineHit) {
            flags |= FLAG_DEADLINE_HIT;
        }

        int loaded = 0, missing = 0;
        for (const auto& entry : grids.byTile) {
            (entry.second ? loaded : missing)++;
        }
        writeHeader(loaded, missing);
        Log::Infof("SunExposureJob: done in %d ms, %d tiles loaded, %d missing, zoom %d..%d, flags %d", elapsedMs(), loaded, missing, maxZoomUsed < 0 ? -1 : minZoomUsed, maxZoomUsed, flags);

        if (options.isReleaseCache()) {
            manager.clearGridCache();
        }
        return result;
    }

}

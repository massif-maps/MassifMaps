#include "ElevationManager.h"
#include "terrain/ElevationNodeField.h"
#include "terrain/ElevationTileGrid.h"
#include "terrain/PrefetchOrder.h"
#include "terrain/TerrainOcclusion.h"
#include "core/BinaryData.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"
#include "datasources/components/TileBitmap.h"
#include "graphics/Bitmap.h"
#include "projections/Projection.h"
#include "rastertiles/ElevationDecoder.h"
#include "rastertiles/MapBoxElevationDataDecoder.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"
#include "utils/Const.h"
#include "utils/Log.h"
#include "utils/TileUtils.h"

#include <vt/RenderStats.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_set>
#include <limits>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace massif {

    static const std::size_t DEFAULT_CACHE_CAPACITY = 64 * 1024 * 1024;
    // A grid count, not a byte budget: a terrain view needs a fixed number of grids whatever the source
    // resolution, and each one past the limit evicts one in use. See docs/internals/rendering/04-terrain.md.
    static const std::size_t MIN_CACHED_GRIDS = 192;
    static const int FAILED_TILE_TTL_MILLISECONDS = 30 * 1000;
    static const int MAX_ANCESTOR_SEARCH_DEPTH = 8;
    // Must hold a panorama's whole cut (~320 tiles, meshCacheSize/2), or it is shed and re-pushed every frame.
    static const std::size_t MAX_PREFETCH_QUEUE_SIZE = 384;
    static const int PREFETCH_THREADS = 3; // elevation tiles are network+decode bound; one worker converges too slowly
    static constexpr double NO_DATA_ELEVATION = -1000000.0;
    static constexpr double DEFAULT_MIN_ELEVATION = -500.0;
    static constexpr double DEFAULT_MAX_ELEVATION = 9000.0;
    static constexpr int RAY_MARCH_MAX_STEPS = 256;
    static constexpr int RAY_BISECT_STEPS = 24;
    // Latitude quantum of the metres-to-internal scale memo, ~40 m of world (see getDisplayScale).
    static const double DISPLAY_SCALE_STEP = Const::WORLD_SIZE / 1048576.0;

    struct ElevationManager::DataSourceListener : public TileDataSource::OnChangeListener {
        explicit DataSourceListener(ElevationManager& manager) : _manager(manager) { }

        virtual void onTilesChanged(bool removeTiles) override {
            _manager.tilesChanged();
        }

    private:
        ElevationManager& _manager;
    };

    unsigned long long ElevationManager::NextInstanceId() {
        static std::atomic<unsigned long long> counter { 0 };
        return counter.fetch_add(1) + 1;
    }

    ElevationManager::ElevationManager(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& elevationDecoder) :
        _dataSource(dataSource),
        _elevationDecoder(ElevationDecoder::Resolve(std::shared_ptr<TileData>(), dataSource, elevationDecoder)),
        _projection(dataSource->getProjection()),
        _dataSourceListener(),
        _instanceId(NextInstanceId()),
        _exaggeration(1.0f),
        _seamlessTileEdges(true),
        _surfaceResolution(32),
        _gridSizeHint(256),
        _maxDataZoom(0),
        _bilinearSurface(false),
        _neighbourPrefetch(true),
        _version(1),
        _dataVersion(1),
        _maxSeenElevation(0.0f),
        _gridCache(DEFAULT_CACHE_CAPACITY),
        _mutex(),
        _prefetchFocusU(0.0),
        _prefetchFocusV(0.0),
        _prefetchFocusValid(false),
        _prefetchStopped(false)
    {
        _dataSourceListener = std::make_shared<DataSourceListener>(*this);
        _dataSource->registerOnChangeListener(_dataSourceListener);
        VT_STAT_INC(elevGridManagers);
    }

    ElevationManager::~ElevationManager() {
        {
            std::lock_guard<std::mutex> lock(_prefetchMutex);
            _prefetchStopped = true;
            _prefetchQueue.clear();
            _prefetchQueueHigh.clear();
            _prefetchTileIds.clear();
        }
        _prefetchCondition.notify_all();
        for (std::thread& thread : _prefetchThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        _dataSource->unregisterOnChangeListener(_dataSourceListener);
        VT_STAT_ADD(elevGridManagers, -1);
    }

    std::shared_ptr<TileDataSource> ElevationManager::getDataSource() const {
        return _dataSource;
    }

    std::shared_ptr<ElevationDecoder> ElevationManager::getElevationDecoder() const {
        return _elevationDecoder;
    }

    float ElevationManager::getExaggeration() const {
        return _exaggeration.load();
    }

    void ElevationManager::setExaggeration(float exaggeration) {
        float value = std::max(0.0f, exaggeration);
        if (_exaggeration.exchange(value) == value) {
            return; // re-setting the same value invalidates every CPU-side height for nothing
        }
        // The DATA version deliberately stands still: heights are scaled on the GPU and the tile
        // surfaces are built flat, so nothing geometric is stale. Only what reads heights on the
        // CPU - label anchors, the raycast - has to catch up, and that watches the global version.
        bumpGlobalVersion();
    }

    bool ElevationManager::isSeamlessTileEdgesEnabled() const {
        return _seamlessTileEdges.load();
    }

    void ElevationManager::setSeamlessTileEdgesEnabled(bool enabled) {
        if (_seamlessTileEdges.exchange(enabled) != enabled) {
            _version++; _dataVersion++; // elevation texture borders change, force a rebuild
        }
    }

    void ElevationManager::setSurfaceResolution(int resolution) {
        int value = std::max(1, resolution);
        if (_surfaceResolution.exchange(value) != value) {
            tilesChanged(); // every grid's node field was built for the old lattice
        }
    }

    bool ElevationManager::isNeighbourPrefetchEnabled() const {
        return _neighbourPrefetch.load();
    }

    void ElevationManager::setNeighbourPrefetchEnabled(bool enabled) {
        _neighbourPrefetch.store(enabled);
    }

    int ElevationManager::getMaxDataZoomCap() const {
        return _maxDataZoom.load();
    }

    bool ElevationManager::isBilinearSurface() const {
        return _bilinearSurface.load();
    }

    void ElevationManager::setBilinearSurface(bool bilinear) {
        if (_bilinearSurface.exchange(bilinear) != bilinear) {
            tilesChanged();
        }
    }

    float ElevationManager::sampleSurfaceHeight(const ElevationTileGrid& grid, double internalX, double internalY) const {
        return (_bilinearSurface.load() ? grid.sampleHeight(internalX, internalY) : grid.sampleNodeHeight(internalX, internalY));
    }

    void ElevationManager::setMaxDataZoomCap(int maxZoom) {
        int value = std::max(0, std::min(maxZoom, Const::MAX_SUPPORTED_ZOOM_LEVEL));
        if (_maxDataZoom.exchange(value) != value) {
            // Dropped, or the finer cached grids would keep disagreeing with capped neighbours.
            tilesChanged();
        }
    }

    std::size_t ElevationManager::getCacheCapacity() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _gridCache.capacity();
    }

    void ElevationManager::setCacheCapacity(std::size_t capacityInBytes) {
        std::lock_guard<std::mutex> lock(_mutex);
        _gridCacheCapacityFixed = true;
        _gridCache.resize(capacityInBytes);
    }

    double ElevationManager::getElevation(const MapPos& pos) const {
        MapPos dataSourcePos = _projection->fromWgs84(pos);
        // TileUtils returns TMS-convention tiles (y=0 south); getTileGrid expects XYZ (y=0 north)
        MapTile mapTile = TileUtils::CalculateMapTile(dataSourcePos, _dataSource->getMaxZoom(), _projection).getFlipped();
        std::shared_ptr<ElevationTileGrid> grid = getTileGrid(mapTile, LoadMode::ALLOW_LOAD);
        if (!grid) {
            Log::Error("ElevationManager::getElevation: no tile found to get elevation");
            return NO_DATA_ELEVATION;
        }
        MapPos internalPos = _projection->toInternal(dataSourcePos);
        return grid->sampleHeight(internalPos.getX(), internalPos.getY());
    }

    std::vector<double> ElevationManager::calculateHorizon(const MapPos& pos, double eyeHeight, const std::vector<double>& azimuths, double maxDistance) const {
        // Refraction modelled as an earth 1/(1 - k) times larger.
        static const double REFRACTION_COEFFICIENT = 0.13;
        static const double FIRST_SAMPLE_METERS = 30.0;
        static const double SAMPLE_GROWTH = 1.008; // ~0.05 degree of the ray a step, near and far alike
        double effectiveRadius = Const::EARTH_RADIUS / (1.0 - REFRACTION_COEFFICIENT);

        MapPos eye = _projection->toInternal(_projection->fromWgs84(pos));
        double metersToInternal = getDisplayScale(eye.getY());
        std::vector<double> horizon(azimuths.size(), -90.0);
        if (!(metersToInternal > 0)) {
            return horizon;
        }
        std::shared_ptr<ElevationTileGrid> eyeGrid = getGridForInternalPos(eye.getX(), eye.getY(), LoadMode::CACHED_ONLY);
        requestTileGridAt(eye.getX(), eye.getY(), eyeGrid ? eyeGrid->getTile().getZoom() : -1, 2);
        if (!eyeGrid) {
            return horizon;
        }
        double eyeZ = eyeGrid->sampleHeight(eye.getX(), eye.getY()) + eyeHeight;

        // Wanted posting ~1/1000 of the distance (under a panorama pixel); coarser grids request it once per tile.
        int maxZoom = dataMaxZoom();
        double worldMeters = Const::EARTH_CIRCUMFERENCE * std::cos(pos.getY() * Const::DEG_TO_RAD);
        std::unordered_set<long long> requested;
        for (std::size_t i = 0; i < azimuths.size(); i++) {
            double azimuth = azimuths[i] * Const::DEG_TO_RAD;
            double dx = std::sin(azimuth), dy = std::cos(azimuth);
            double best = -90.0;
            for (double distance = FIRST_SAMPLE_METERS; distance <= maxDistance; distance = std::max(distance * SAMPLE_GROWTH, distance + FIRST_SAMPLE_METERS)) {
                double x = eye.getX() + dx * distance * metersToInternal;
                double y = eye.getY() + dy * distance * metersToInternal;
                std::shared_ptr<ElevationTileGrid> grid = getGridForInternalPos(x, y, LoadMode::CACHED_ONLY);
                int wantedZoom = std::max(0, std::min(maxZoom, static_cast<int>(std::floor(std::log2(worldMeters / (DEM_TEXELS_PER_TILE_UNIT * std::max(FIRST_SAMPLE_METERS, distance * 0.001)))))));
                if (!grid || grid->getTile().getZoom() < wantedZoom) {
                    MapTile tile = getTileForInternalPos(x, y);
                    while (tile.getZoom() > wantedZoom) {
                        tile = tile.getParent();
                    }
                    if (requested.insert(tile.getTileId()).second) {
                        requestTileGrid(tile, 0);
                    }
                }
                if (!grid) {
                    continue;
                }
                double drop = distance * distance / (2.0 * effectiveRadius);
                double altitude = std::atan2(grid->sampleHeight(x, y) - drop - eyeZ, distance) * Const::RAD_TO_DEG;
                best = std::max(best, altitude);
            }
            horizon[i] = best;
        }
        return horizon;
    }

    std::vector<double> ElevationManager::getElevations(const std::vector<MapPos>& poses) const {
        std::vector<double> results;
        results.reserve(poses.size());
        for (const MapPos& pos : poses) {
            MapPos dataSourcePos = _projection->fromWgs84(pos);
            MapTile mapTile = TileUtils::CalculateMapTile(dataSourcePos, _dataSource->getMaxZoom(), _projection).getFlipped();
            std::shared_ptr<ElevationTileGrid> grid = getTileGrid(mapTile, LoadMode::ALLOW_LOAD);
            if (grid) {
                MapPos internalPos = _projection->toInternal(dataSourcePos);
                results.push_back(grid->sampleHeight(internalPos.getX(), internalPos.getY()));
            } else {
                results.push_back(NO_DATA_ELEVATION);
            }
        }
        return results;
    }

    double ElevationManager::getElevationMeters(double internalX, double internalY, LoadMode mode) const {
        double wrappedX = wrapInternalX(internalX);
        std::shared_ptr<ElevationTileGrid> grid = getGridForInternalPos(wrappedX, internalY, mode);
        if (!grid) {
            return 0.0;
        }
        return grid->sampleHeight(wrappedX, internalY);
    }

    double ElevationManager::getDisplayHeight(double internalX, double internalY, LoadMode mode) const {
        // The SURFACE's height, not the DEM's: the node field is what the ground is drawn from,
        // and a label anchor or a raycast that took the DEM instead would miss the ground by the
        // relief the mesh cannot carry (metres, on a lidar DEM under a coarse cell).
        double wrappedX = wrapInternalX(internalX);
        std::shared_ptr<ElevationTileGrid> grid = getGridForInternalPos(wrappedX, internalY, mode);
        if (!grid) {
            return 0.0;
        }
        return sampleSurfaceHeight(*grid, wrappedX, internalY) * _exaggeration.load() * getDisplayScale(internalY);
    }

    bool ElevationManager::getDisplayHeightCached(double internalX, double internalY, double& height) const {
        int resolvedZoom = -1;
        return getDisplayHeightCached(internalX, internalY, height, resolvedZoom);
    }

    bool ElevationManager::getDisplayHeightCached(double internalX, double internalY, double& height, int& resolvedZoom) const {
        // getDisplayHeight cannot say whether it HAS data - it returns 0 either way, and 0 is a legal
        // height. An extrusion given a base of 0 where the ground is 215 m sinks below the terrain.
        double wrappedX = wrapInternalX(internalX);
        std::shared_ptr<ElevationTileGrid> grid = getGridForInternalPos(wrappedX, internalY, LoadMode::CACHED_ONLY);
        if (!grid) {
            return false;
        }
        // Reports which grid answered: a cached-only read may fall back to a much coarser ancestor.
        resolvedZoom = grid->getTile().getZoom();
        height = sampleSurfaceHeight(*grid, wrappedX, internalY) * _exaggeration.load() * getDisplayScale(internalY);
        return true;
    }

    void ElevationManager::getDisplayGradient(double internalX, double internalY, LoadMode mode, double& dhdx, double& dhdy) const {
        dhdx = 0;
        dhdy = 0;
        double wrappedX = wrapInternalX(internalX);
        std::shared_ptr<ElevationTileGrid> grid = getGridForInternalPos(wrappedX, internalY, mode);
        if (!grid) {
            return;
        }
        float gradX = 0, gradY = 0;
        grid->sampleGradient(wrappedX, internalY, gradX, gradY);
        double scale = _exaggeration.load() * getDisplayScale(internalY);
        dhdx = gradX * scale;
        dhdy = gradY * scale;
    }

    std::shared_ptr<ElevationTileGrid> ElevationManager::getTileGrid(const MapTile& mapTile, LoadMode mode) const {
        return lookupTileGrid(clampTileZoom(mapTile), mode);
    }

    std::shared_ptr<ElevationTileGrid> ElevationManager::getDataTileGrid(const MapTile& dataTile, LoadMode mode) const {
        return lookupTileGrid(clampDataTileZoom(dataTile), mode);
    }

    bool ElevationManager::readCachedGrid(long long tileId, std::shared_ptr<ElevationTileGrid>& grid) const {
        if (!_gridCache.read(tileId, grid)) {
            return false;
        }
        // timed_lru_cache::read ignores expiry (only valid() checks it), which would make a failure marker permanent.
        return grid || _gridCache.valid(tileId);
    }

    std::shared_ptr<ElevationTileGrid> ElevationManager::lookupTileGrid(const MapTile& tile, LoadMode mode) const {
        if (tile.getZoom() < _dataSource->getMinZoom()) {
            return std::shared_ptr<ElevationTileGrid>();
        }

        // Per-thread memo of the last resolved (tile -> grid); grids are immutable and versioned.
        // MODE is part of the key - CACHED_ONLY takes a coarse ancestor where ALLOW_LOAD loads the
        // tile, and sharing them makes two consumers of the same ground disagree by the chord error.
        struct GridMemo {
            unsigned long long instanceId = 0;
            unsigned int version = 0;
            long long tileId = -1;
            LoadMode mode = LoadMode::CACHED_ONLY;
            std::shared_ptr<ElevationTileGrid> grid;
        };
        static thread_local GridMemo memo;
        unsigned int memoVersion = _version.load();
        bool memoizable = (mode != LoadMode::LOAD_EXACT);
        if (memoizable && memo.instanceId == _instanceId && memo.version == memoVersion && memo.tileId == tile.getTileId() && memo.mode == mode && memo.grid) {
            return memo.grid;
        }

        bool tileFailed = false;
        if (mode == LoadMode::LOAD_EXACT) {
            // LOAD_EXACT wants THIS level: a cached ancestor must not short-circuit the load, or
            // neighbours displaced by different levels tear the surface open. A grid cached under
            // this tile id is the data source saying the level does not exist here, so it stands.
            std::lock_guard<std::mutex> lock(_mutex);
            std::shared_ptr<ElevationTileGrid> grid;
            if (readCachedGrid(tile.getTileId(), grid)) {
                if (grid) {
                    return grid;
                }
                tileFailed = true; // recently failed to load, do not retry until the failure marker expires
            }
        } else {
            std::lock_guard<std::mutex> lock(_mutex);
            MapTile searchTile = tile;
            int maxDepth = (mode == LoadMode::CACHED_EXACT ? 0 : MAX_ANCESTOR_SEARCH_DEPTH);
            for (int depth = 0; depth <= maxDepth; depth++) {
                std::shared_ptr<ElevationTileGrid> grid;
                if (readCachedGrid(searchTile.getTileId(), grid)) {
                    if (grid) {
#if MASSIF_VT_RENDER_STATS
                        if (grid->getTile() == tile) { VT_STAT_INC(elevExactHits); }
                        else if (searchTile == tile) { VT_STAT_INC(elevAncestorAliasHits); }
                        else { VT_STAT_INC(elevAncestorWalkHits); }
#endif
                        memo = GridMemo { _instanceId, memoVersion, tile.getTileId(), mode, grid };
                        return grid;
                    }
                    if (searchTile == tile) {
                        tileFailed = true; // recently failed to load, do not retry until the failure marker expires
                    }
                }
                if (searchTile.getZoom() <= _dataSource->getMinZoom()) {
                    break;
                }
                searchTile = searchTile.getParent();
            }
        }

        if (mode == LoadMode::CACHED_ONLY || mode == LoadMode::CACHED_EXACT || tileFailed) {
            return std::shared_ptr<ElevationTileGrid>();
        }

        // Single-flight: many layer tiles share one clamped elevation tile, so only the first caller
        // loads it and the others wait for its result.
        long long tileId = tile.getTileId();
        std::promise<std::shared_ptr<ElevationTileGrid> > promise;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            auto it = _pendingLoads.find(tileId);
            if (it != _pendingLoads.end()) {
                std::shared_future<std::shared_ptr<ElevationTileGrid> > future = it->second;
                lock.unlock();
                return future.get();
            }
            _pendingLoads[tileId] = promise.get_future().share();
        }

        // Load and decode outside of the lock
        std::shared_ptr<ElevationTileGrid> grid;
        try {
            grid = loadTileGrid(tile);
        }
        catch (...) {
            std::lock_guard<std::mutex> lock(_mutex);
            _pendingLoads.erase(tileId);
            promise.set_value(std::shared_ptr<ElevationTileGrid>());
            throw;
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (grid) {
                // Grow the budget to the source's own grid size before inserting, so the cache is
                // a tile count rather than a byte count the raster resolution decides (see
                // MIN_CACHED_GRIDS). Only ever grows, and a caller that set its own capacity keeps it.
                std::size_t minCapacity = grid->getDataSize() * MIN_CACHED_GRIDS;
                if (!_gridCacheCapacityFixed && _gridCache.capacity() < minCapacity) {
                    Log::Infof("ElevationManager: elevation grid cache %d -> %d MB (%d grids of %d KB)",
                               static_cast<int>(_gridCache.capacity() >> 20), static_cast<int>(minCapacity >> 20),
                               static_cast<int>(MIN_CACHED_GRIDS), static_cast<int>(grid->getDataSize() >> 10));
                    _gridCache.resize(minCapacity);
                }
#if MASSIF_VT_RENDER_STATS
                // A second arrival means the grid was evicted while still in use.
                if (!_everLoadedTiles.insert(grid->getTile().getTileId()).second) {
                    VT_STAT_INC(elevGridReinserts);
                }
                // Max across managers: the gauge is global and several managers write it.
                {
                    long long mine = static_cast<long long>(_everLoadedTiles.size());
                    long long seen = vt::RenderStats::elevGridDistinctEver.load();
                    while (mine > seen && !vt::RenderStats::elevGridDistinctEver.compare_exchange_weak(seen, mine)) { }
                }
                VT_STAT_SET(elevGridSizeKB, static_cast<long long>(grid->getDataSize() >> 10));
                VT_STAT_INC(elevGridInserts);
#endif
                _gridCache.put(grid->getTile().getTileId(), grid, grid->getDataSize());
                VT_STAT_SET(elevGridBytes, static_cast<long long>(_gridCache.size()));
                VT_STAT_SET(elevGridCapacity, static_cast<long long>(_gridCache.capacity()));
                if (grid->getTile() != tile) {
                    // Loaded an ancestor (replace-with-parent); also mark the requested tile as resolved via ancestor
                    VT_STAT_INC(elevAncestorAliasPuts);
                    _gridCache.put(tileId, grid, 1024);
                }
                float maxSeen = _maxSeenElevation.load();
                while (grid->getMaxHeight() > maxSeen && !_maxSeenElevation.compare_exchange_weak(maxSeen, grid->getMaxHeight())) { }

                // Record WHICH tile changed under the insert's lock, and bump the data version too -
                // a decoded tile IS new data. docs/internals/rendering/04-terrain.md, the two versions.
                _dataVersion++;
                unsigned int version = _version.fetch_add(1) + 1;
                _changeLog.emplace_back(version, grid->getTile());
                while (_changeLog.size() > MAX_CHANGE_LOG_ENTRIES) {
                    _changeLog.pop_front();
                    _changeLogFirstVersion = _changeLog.front().first;
                }
            } else {
                _gridCache.put(tileId, std::shared_ptr<ElevationTileGrid>(), 1024);
                _gridCache.invalidate(tileId, std::chrono::steady_clock::now() + std::chrono::milliseconds(FAILED_TILE_TTL_MILLISECONDS));
            }
            _pendingLoads.erase(tileId);
        }
        if (grid) {
            notifyDataChanged();
        }
        promise.set_value(grid);
        return grid;
    }

    void ElevationManager::setDataChangedListener(const std::function<void()>& listener) {
        std::lock_guard<std::mutex> lock(_mutex);

        _dataChangedListener = listener;
    }

    void ElevationManager::notifyDataChanged() const {
        std::function<void()> listener;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            listener = _dataChangedListener;
        }
        // Outside the lock: the listener asks the renderer for a frame, and that frame reads
        // this manager back.
        if (listener) {
            listener();
        }
    }

    int ElevationManager::getDetailZoomLimit() const {
        // The inverse of clampTileZoom: the source maximum plus the levels it drops for an oversized grid.
        int bias = 0;
        for (int size = _gridSizeHint.load(); size > DEM_TEXELS_PER_TILE_UNIT; size /= 2) {
            bias++;
        }
        // Not dataMaxZoom(): this is the LOD floor, and capping it tessellates coarse tiles into
        // thousands of sub-surfaces. The cap belongs in tile selection (clampDataTileZoom) only.
        return _dataSource->getMaxZoom() + bias;
    }

    MapTile ElevationManager::getTileForInternalPos(double internalX, double internalY) const {
        MapPos dataSourcePos = _projection->fromInternal(MapPos(wrapInternalX(internalX), internalY, 0));
        return TileUtils::CalculateClippedMapTile(dataSourcePos, dataMaxZoom(), _projection).getFlipped();
    }

    MapTile ElevationManager::getDataTile(const MapTile& mapTile) const {
        return clampTileZoom(mapTile);
    }

    MapTile ElevationManager::getDetailDataTile(const MapTile& mapTile, int extraLevels) const {
        // Kept for callers that ask for MORE than the standard rule gives; the rule itself no
        // longer holds anything back (see clampTileZoom), so extraLevels only ever removes the
        // source zoom bias, and never goes below the tile's own zoom.
        if (extraLevels <= 0) {
            return clampTileZoom(mapTile);
        }
        MapTile tile = mapTile;
        int limit = DEM_TEXELS_PER_TILE_UNIT << extraLevels;
        for (int size = _gridSizeHint.load(); size > limit && tile.getZoom() > 0; size /= 2) {
            tile = tile.getParent();
        }
        return clampDataTileZoom(tile);
    }

    MapTile ElevationManager::getFullDetailDataTile(const MapTile& mapTile) const {
        // No mesh-resolution cap: a per-fragment consumer (hillshade shading) resolves relief the
        // surface geometry cannot, so capping it there leaves it blurred by two zoom levels.
        return clampDataTileZoom(mapTile);
    }

    void ElevationManager::prefetchTileGrid(const MapTile& dataTile, int priority) const {
        if (!_neighbourPrefetch.load()) {
            return;
        }
        requestTileGrid(dataTile, priority);
    }

    void ElevationManager::requestTileGrid(const MapTile& dataTile, int priority) const {
        MapTile tile = clampDataTileZoom(dataTile);
        if (tile.getZoom() < _dataSource->getMinZoom()) {
            return;
        }

        long long tileId = tile.getTileId();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            std::shared_ptr<ElevationTileGrid> grid;
            if (readCachedGrid(tileId, grid)) {
                return; // already loaded, resolved via an ancestor, or recently failed
            }
            if (_pendingLoads.find(tileId) != _pendingLoads.end()) {
                return; // already being loaded by another thread
            }
        }

        {
            std::lock_guard<std::mutex> lock(_prefetchMutex);
            if (_prefetchStopped) {
                return;
            }
            if (!_prefetchTileIds.insert(tileId).second) {
                return; // already queued
            }
            std::deque<PrefetchEntry>& queue = (priority >= 2 ? _prefetchQueueHigh : _prefetchQueue);
            queue.push_back(PrefetchEntry { tile, priority });
            bool haveFocus = _prefetchFocusValid.load();
            double focusU = _prefetchFocusU.load(), focusV = _prefetchFocusV.load();
            while (queue.size() > MAX_PREFETCH_QUEUE_SIZE) {
                // Shed the least useful entry, not the oldest: lowest priority first (corners before edge
                // neighbours), then the furthest from the camera, so loading converges outwards repeatably.
                auto victim = queue.begin();
                double victimDistance = (haveFocus ? prefetchTileDistance(victim->tile, focusU, focusV) : 0.0);
                for (auto it = queue.begin(); it != queue.end(); it++) {
                    double distance = (haveFocus ? prefetchTileDistance(it->tile, focusU, focusV) : 0.0);
                    if (it->priority < victim->priority || (it->priority == victim->priority && distance > victimDistance)) {
                        victim = it;
                        victimDistance = distance;
                    }
                }
                _prefetchTileIds.erase(victim->tile.getTileId());
                queue.erase(victim);
            }
            while (static_cast<int>(_prefetchThreads.size()) < PREFETCH_THREADS) {
                _prefetchThreads.emplace_back([this]() { runPrefetchWorker(); });
            }
        }
        _prefetchCondition.notify_one();
    }

    void ElevationManager::requestTileGridAt(double internalX, double internalY, int resolvedZoom, int priority) const {
        MapTile tile = getDataTile(getTileForInternalPos(internalX, internalY));
        if (resolvedZoom < tile.getZoom()) {
            requestTileGrid(tile, priority);
        }
    }

    void ElevationManager::setPrefetchFocus(double internalX, double internalY) const {
        _prefetchFocusU.store(internalX / Const::WORLD_SIZE + 0.5);
        _prefetchFocusV.store(0.5 - internalY / Const::WORLD_SIZE); // XYZ convention: v grows south, as tile y does
        _prefetchFocusValid.store(true);
    }

    void ElevationManager::runPrefetchWorker() const {
        while (true) {
            MapTile tile(0, 0, 0, 0);
            {
                std::unique_lock<std::mutex> lock(_prefetchMutex);
                _prefetchCondition.wait(lock, [this]() { return _prefetchStopped || !_prefetchQueue.empty() || !_prefetchQueueHigh.empty(); });
                if (_prefetchStopped) {
                    return;
                }
                std::deque<PrefetchEntry>& queue = (_prefetchQueueHigh.empty() ? _prefetchQueue : _prefetchQueueHigh);
                // Priority first, then nearest the camera. Distance orders within a priority and
                // never overrules it: a tile displaced by an ancestor grid tears against its
                // neighbours. With no focus set it is newest first, as the queue always drained.
                bool haveFocus = _prefetchFocusValid.load();
                double focusU = _prefetchFocusU.load(), focusV = _prefetchFocusV.load();
                std::size_t index = 0;
                int bestPriority = std::numeric_limits<int>::min();
                double bestDistance = std::numeric_limits<double>::infinity();
                for (std::size_t i = 0; i < queue.size(); i++) {
                    int priority = queue[i].priority;
                    if (priority < bestPriority) {
                        continue;
                    }
                    double distance = (haveFocus ? prefetchTileDistance(queue[i].tile, focusU, focusV) : 0.0);
                    if (priority > bestPriority || distance < bestDistance || !haveFocus) {
                        bestPriority = priority;
                        bestDistance = distance;
                        index = i;
                    }
                }
                tile = queue[index].tile;
                queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(index));
                _prefetchTileIds.erase(tile.getTileId());
            }
            try {
                getDataTileGrid(tile, LoadMode::LOAD_EXACT); // queued tiles are elevation tiles already
            }
            catch (const std::exception& ex) {
                Log::Warnf("ElevationManager::runPrefetchWorker: Failed to prefetch elevation tile: %s", ex.what());
            }
        }
    }

    double ElevationManager::getDisplayScale(double internalY) const {
        // tanh + expm1 are hot here, so memo per DISPLAY_SCALE_STEP (~4e-7 relative scale). Quantising
        // rather than interpolating keeps the height a function of position alone, so nothing oscillates.
        double step = std::floor(internalY / DISPLAY_SCALE_STEP + 0.5);
        struct ScaleMemo {
            double step = std::numeric_limits<double>::quiet_NaN();
            double scale = 0;
        };
        static thread_local ScaleMemo memo;
        if (memo.step == step) {
            return memo.scale;
        }
        double sin = std::tanh(step * DISPLAY_SCALE_STEP * 2 * Const::PI / Const::WORLD_SIZE);
        double cos = std::sqrt(std::max(1.0e-6, 1.0 - sin * sin));
        double scale = Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE / cos;
        memo = ScaleMemo { step, scale };
        return scale;
    }

    void ElevationManager::getDisplayHeightRange(double internalY, double& minZ, double& maxZ) const {
        getDisplayHeightRange(internalY, _exaggeration.load(), minZ, maxZ);
    }

    void ElevationManager::getDisplayHeightRange(double internalY, float exaggeration, double& minZ, double& maxZ) const {
        double maxMeters = std::max(static_cast<double>(_maxSeenElevation.load()), DEFAULT_MAX_ELEVATION);
        double scale = exaggeration * getDisplayScale(internalY);
        minZ = DEFAULT_MIN_ELEVATION * scale;
        maxZ = maxMeters * scale;
    }

    double ElevationManager::getDisplayHeight(double internalX, double internalY) const {
        return getDisplayHeight(internalX, internalY, LoadMode::CACHED_ONLY);
    }

    void ElevationManager::getDisplayGradient(double internalX, double internalY, double& dhdx, double& dhdy) const {
        getDisplayGradient(internalX, internalY, LoadMode::CACHED_ONLY, dhdx, dhdy);
    }

    int ElevationManager::getMaxDataZoom() const {
        // The source's depth, not dataMaxZoom(): it sizes the projection surface's subdivision
        // (CalculateSplitThreshold), and the working-set cap must not move tessellation.
        if (std::shared_ptr<TileDataSource> dataSource = getDataSource()) {
            return dataSource->getMaxZoom();
        }
        return -1;
    }

    bool ElevationManager::intersectRay(const cglib::ray3<double>& ray, double& t) const {
        if (ray.direction(2) >= 0) {
            return false; // upward/horizontal rays can not hit terrain from above
        }

        float exaggeration = _exaggeration.load();
        double maxElevation = std::max(static_cast<double>(_maxSeenElevation.load()), DEFAULT_MAX_ELEVATION);

        // Most march steps hit the same elevation grid; keep the last used grid around
        // to avoid a cache lookup (and its mutex) per sample.
        std::shared_ptr<ElevationTileGrid> cachedGrid;
        auto sampleDisplayHeight = [&](double internalX, double internalY) -> double {
            double wrappedX = wrapInternalX(internalX);
            if (!cachedGrid || !cachedGrid->getInternalBounds().contains(MapPos(wrappedX, internalY, 0))) {
                cachedGrid = getGridForInternalPos(wrappedX, internalY, LoadMode::CACHED_ONLY);
            }
            if (!cachedGrid) {
                return 0.0;
            }
            return sampleSurfaceHeight(*cachedGrid, wrappedX, internalY) * exaggeration * getDisplayScale(internalY);
        };

        // Conservative display-space search interval. Use the largest latitude scale along the ray
        // to be safe; heights are re-sampled precisely at each march step anyway.
        double tGround = -ray.origin(2) / ray.direction(2);
        double scale0 = getDisplayScale(ray.origin(1));
        double scale1 = getDisplayScale(ray(tGround)(1));
        double maxScale = std::max(scale0, scale1);
        double zTop = maxElevation * exaggeration * maxScale;
        double zBottom = DEFAULT_MIN_ELEVATION * exaggeration * maxScale;

        double t0 = 0;
        if (ray.origin(2) > zTop) {
            t0 = (zTop - ray.origin(2)) / ray.direction(2);
        }
        double t1 = (zBottom - ray.origin(2)) / ray.direction(2);
        if (!(t1 > t0)) {
            return false;
        }

        // March with quadratically increasing steps (dense near the origin, coarse far away),
        // then refine the first crossing with bisection.
        double prevT = t0;
        cglib::vec3<double> pos = ray(t0);
        double prevDelta = pos(2) - sampleDisplayHeight(pos(0), pos(1));
        if (prevDelta <= 0) {
            t = t0;
            return true;
        }
        for (int i = 1; i <= RAY_MARCH_MAX_STEPS; i++) {
            double f = static_cast<double>(i) / RAY_MARCH_MAX_STEPS;
            double curT = t0 + (t1 - t0) * f * f;
            pos = ray(curT);
            double delta = pos(2) - sampleDisplayHeight(pos(0), pos(1));
            if (delta <= 0) {
                double tLow = prevT;
                double tHigh = curT;
                for (int j = 0; j < RAY_BISECT_STEPS; j++) {
                    double tMid = (tLow + tHigh) * 0.5;
                    pos = ray(tMid);
                    double midDelta = pos(2) - sampleDisplayHeight(pos(0), pos(1));
                    if (midDelta <= 0) {
                        tHigh = tMid;
                    } else {
                        tLow = tMid;
                    }
                }
                t = (tLow + tHigh) * 0.5;
                return true;
            }
            prevT = curT;
            prevDelta = delta;
        }
        return false;
    }

    bool ElevationManager::isSegmentBlocked(const cglib::vec3<double>& from, const cglib::vec3<double>& to, double maxFraction) const {
        // Steps grow with the distance, as in calculateHorizon: ~0.01 degree of the line each, from 30 m.
        static const double FIRST_STEP_METERS = 30.0;
        static const double STEP_GROWTH = 1.015;

        float exaggeration = _exaggeration.load();
        double scale = getDisplayScale(from(1));
        double zTop = std::max(static_cast<double>(_maxSeenElevation.load()), DEFAULT_MAX_ELEVATION) * exaggeration * scale;
        double groundMargin = 0;
        if (std::shared_ptr<ElevationTileGrid> targetGrid = getGridForInternalPos(wrapInternalX(to(0)), to(1), LoadMode::CACHED_ONLY)) {
            groundMargin = targetGrid->getInternalBounds().getDelta().getX() / std::max(1, targetGrid->getWidth() - 1);
        }
        std::shared_ptr<ElevationTileGrid> grid;
        auto groundAt = [this, &grid, exaggeration](double internalX, double internalY, double& height) {
            double x = wrapInternalX(internalX);
            if (!grid || !grid->getInternalBounds().contains(MapPos(x, internalY, 0))) {
                grid = getGridForInternalPos(x, internalY, LoadMode::CACHED_ONLY);
            }
            if (!grid) {
                return false;
            }
            height = sampleSurfaceHeight(*grid, x, internalY) * exaggeration * getDisplayScale(internalY);
            return true;
        };
        return TerrainOcclusion::isSegmentBlocked(from, to, maxFraction, FIRST_STEP_METERS * scale, STEP_GROWTH, zTop, groundMargin, groundAt);
    }

    void ElevationManager::getMinMaxDisplayHeight(const MapTile& tile, double& minZ, double& maxZ) const {
        getMinMaxDisplayHeight(tile, minZ, maxZ, false);
    }

    void ElevationManager::getMinMaxDisplayHeightExact(const MapTile& tile, double& minZ, double& maxZ) const {
        getMinMaxDisplayHeight(tile, minZ, maxZ, true);
    }

    bool ElevationManager::getMinMaxDisplayHeightCached(const MapTile& tile, double& minZ, double& maxZ) const {
        return getMinMaxDisplayHeight(tile, minZ, maxZ, true);
    }

    bool ElevationManager::getMinMaxDisplayHeight(const MapTile& tile, double& minZ, double& maxZ, bool exact) const {
        // Fall back to the max elevation seen so far, not a large constant: a kilometres-high bound
        // pulls far tiles into the frustum, they fetch data, their bounds change - the set churns.
        double minMeters = 0;
        double maxMeters = _maxSeenElevation.load();
        bool haveData = false;
        MapBounds bounds = TileUtils::CalculateMapTileBounds(tile.getFlipped(), _projection);
        if (std::shared_ptr<ElevationTileGrid> grid = getTileGrid(tile, LoadMode::CACHED_ONLY)) {
            minMeters = grid->getMinHeight();
            maxMeters = grid->getMaxHeight();
            haveData = true;
        }
        MapPos internalCenter = _projection->toInternal(bounds.getCenter());
        MapPos internalMin = _projection->toInternal(bounds.getMin());
        MapPos internalMax = _projection->toInternal(bounds.getMax());
        double scale = std::max(getDisplayScale(internalMin.getY()), std::max(getDisplayScale(internalMax.getY()), getDisplayScale(internalCenter.getY())));
        double exaggeration = _exaggeration.load();
        // Bounds normally include sea level, so a tile without data still has a usable range. A
        // caller fitting a box rather than culling wants the real span: the extra slab is divided by
        // tan(sun altitude), so at a low sun it costs kilometres of box and coarser texels.
        if (exact && haveData) {
            minZ = minMeters * exaggeration * scale;
            maxZ = maxMeters * exaggeration * scale;
            return true;
        }
        minZ = std::min(0.0, minMeters * exaggeration * scale);
        maxZ = std::max(0.0, maxMeters * exaggeration * scale);
        return haveData;
    }

    unsigned int ElevationManager::getDataVersion() const {
        return _dataVersion.load();
    }

    unsigned int ElevationManager::getVersion() const {
        return _version.load();
    }

    bool ElevationManager::getChangedTiles(unsigned int sinceVersion, std::vector<MapTile>& tiles) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (sinceVersion + 1 < _changeLogFirstVersion) {
            return false;
        }
        for (const std::pair<unsigned int, MapTile>& entry : _changeLog) {
            if (entry.first > sinceVersion) {
                tiles.push_back(entry.second);
            }
        }
        return true;
    }

    void ElevationManager::tilesChanged() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _gridCache.clear();
        }
        _dataVersion++;
        bumpGlobalVersion();
        notifyDataChanged();
    }

    void ElevationManager::bumpGlobalVersion() {
        std::lock_guard<std::mutex> lock(_mutex);

        // Everything derived from the elevation data is stale at once, which the per-tile
        // change log can not express - drop it so that consumers take the full
        // invalidation path until the next tile-level change.
        unsigned int version = _version.fetch_add(1) + 1;
        _changeLog.clear();
        _changeLogFirstVersion = version + 1;
    }

    double ElevationManager::wrapInternalX(double internalX) const {
        double worldSize = Const::WORLD_SIZE;
        return internalX - worldSize * std::floor(internalX / worldSize + 0.5);
    }

    // Mesh cells a node averages the DEM over (ElevationNodeField::DEFAULT_BOX_CELLS), with the
    // measurement override:  adb shell setprop debug.massif.nodebox <cells>
#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
    int ElevationManager::nodeBoxCells() {
        static const int cells = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.nodebox", property) > 0) {
                int value = std::atoi(property);
                if (value > 0) {
                    return value;
                }
            }
            return ElevationNodeField::DEFAULT_BOX_CELLS;
        }();
        return cells;
    }
#else
    int ElevationManager::nodeBoxCells() {
        return ElevationNodeField::DEFAULT_BOX_CELLS;
    }
#endif

    MapTile ElevationManager::clampTileZoom(const MapTile& mapTile) const {
        // Tangram's rule verbatim (RasterSource::addRasterTask). Deliberately NOT idempotent -
        // applying it twice costs another level per hop, so those callers use clampDataTileZoom.
        MapTile tile = mapTile;
        for (int size = _gridSizeHint.load(); size > DEM_TEXELS_PER_TILE_UNIT && tile.getZoom() > 0; size /= 2) {
            tile = tile.getParent();
        }
        return clampDataTileZoom(tile);
    }

    int ElevationManager::dataMaxZoom() const {
        int cap = _maxDataZoom.load();
        int sourceMax = _dataSource->getMaxZoom();
        return cap > 0 ? std::min(cap, sourceMax) : sourceMax;
    }

    MapTile ElevationManager::clampDataTileZoom(const MapTile& dataTile) const {
        // Only the data source zoom range: idempotent, safe to apply to an elevation tile.
        MapTile tile = dataTile;
        int maxZoom = dataMaxZoom();
        while (tile.getZoom() > maxZoom) {
            tile = tile.getParent();
        }
        return tile;
    }

    std::shared_ptr<ElevationTileGrid> ElevationManager::getGridForInternalPos(double internalX, double internalY, LoadMode mode) const {
        // A label re-anchor samples every label vertex and the tile math before the lookup is hot. Grids are
        // immutable and versioned, so the last grid containing the point stays right. LOAD_EXACT excluded.
        struct PosMemo {
            unsigned long long instanceId = 0;
            unsigned int version = 0;
            LoadMode mode = LoadMode::CACHED_ONLY;
            std::shared_ptr<ElevationTileGrid> grid;
        };
        static thread_local PosMemo memo;
        unsigned int memoVersion = _version.load();
        bool memoizable = (mode != LoadMode::LOAD_EXACT);
        if (memoizable && memo.instanceId == _instanceId && memo.version == memoVersion && memo.mode == mode && memo.grid) {
            if (memo.grid->getInternalBounds().contains(MapPos(internalX, internalY, 0))) {
                return memo.grid;
            }
        }

        std::shared_ptr<ElevationTileGrid> grid = getTileGrid(getTileForInternalPos(internalX, internalY), mode);
        if (memoizable && grid) {
            memo = PosMemo { _instanceId, memoVersion, mode, grid };
        }
        return grid;
    }

    std::shared_ptr<ElevationTileGrid> ElevationManager::loadTileGrid(const MapTile& requestedTile) const {
        // The tile is in XYZ convention (y=0 north), which is what TileDataSource::loadTile expects.
        // TileUtils works in TMS convention (y=0 south), hence the getFlipped() for bounds math.
        MapTile mapTile = requestedTile;
        std::shared_ptr<TileData> tileData = _dataSource->loadTile(mapTile);
        while (tileData && tileData->isReplaceWithParent() && mapTile.getZoom() > 0) {
            mapTile = mapTile.getParent();
            tileData = _dataSource->loadTile(mapTile);
        }
        if (!tileData || !tileData->getData()) {
            return std::shared_ptr<ElevationTileGrid>();
        }

        std::shared_ptr<Bitmap> tileBitmap = DecodeTileBitmap(tileData);
        if (!tileBitmap) {
            Log::Error("ElevationManager::loadTileGrid: Failed to decode elevation tile bitmap");
            return std::shared_ptr<ElevationTileGrid>();
        }

        MapBounds bounds = TileUtils::CalculateMapTileBounds(mapTile.getFlipped(), _projection);
        MapPos internalMin = _projection->toInternal(bounds.getMin());
        MapPos internalMax = _projection->toInternal(bounds.getMax());
        MapBounds internalBounds(MapPos(std::min(internalMin.getX(), internalMax.getX()), std::min(internalMin.getY(), internalMax.getY())),
                                 MapPos(std::max(internalMin.getX(), internalMax.getX()), std::max(internalMin.getY(), internalMax.getY())));

        // Per TILE, not per source: two DEM sources of different encodings can sit behind one
        // OrderedTileDataSource. ElevationTileGrid then carries these coefficients to the CPU
        // sampler and to the GPU alike, so nothing downstream has to agree on one encoding.
        std::array<double, 4> coeffs = ElevationDecoder::Resolve(tileData, _dataSource, _elevationDecoder)->getColorComponentCoefficients();
        // Mesh nodes across this grid: the lattice is per RENDER tile, and clampTileZoom hands a
        // grid to one render tile per DEM_TEXELS_PER_TILE_UNIT texels of its edge.
        int renderTilesPerEdge = std::max(1, static_cast<int>(tileBitmap->getWidth()) / DEM_TEXELS_PER_TILE_UNIT);
        int nodesPerEdge = _surfaceResolution.load() * renderTilesPerEdge;
        std::shared_ptr<ElevationTileGrid> grid = ElevationTileGrid::DecodeBitmap(mapTile, internalBounds, tileBitmap, coeffs, nodesPerEdge, nodeBoxCells());
        if (grid && grid->getWidth() > 0) {
            _gridSizeHint.store(grid->getWidth()); // drives the elevation level cap in clampTileZoom
        }
        return grid;
    }
}

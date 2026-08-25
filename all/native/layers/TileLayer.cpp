#include "TileLayer.h"
#include "core/BinaryData.h"
#include "components/Exceptions.h"
#include "components/CancelableTask.h"
#include "datasources/components/TileData.h"
#include "layers/TileLoadListener.h"
#include "layers/UTFGridEventListener.h"
#include "layers/components/UTFGridTile.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "renderers/components/CullState.h"
#include "renderers/components/RayIntersectedElement.h"
#include "renderers/MapRenderer.h"
#include "renderers/TileRenderer.h"
#include "projections/Projection.h"
#include "projections/EPSG3857.h"
#include "terrain/ElevationManager.h"
#include "terrain/TerrainTileTransformer.h"
#include "ui/UTFGridClickInfo.h"
#include "utils/Const.h"
#include "utils/TileUtils.h"
#include "utils/Log.h"

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include <vt/TileTransformer.h>
#include <vt/RenderStats.h>

namespace massif {

#ifdef __ANDROID__
    static bool isLineSourceDensityForced() {
        static const bool forced = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return __system_property_get("debug.massif.linesourcedensity", property) > 0 && property[0] == '1';
        }();
        return forced;
    }

    // Measurement switch for what AREA subdivision costs: it is the expensive half (a triangle
    // subdivides 1/factor^2, see TerrainTileTransformer.h) and it is on for correctness, not for
    // speed - an un-subdivided fill floats above the ground and hides every ground-shaped draw
    // stacked after it. Off = the shipped behaviour.
    //   adb shell setprop debug.massif.areasourcedensity 1
    static bool isAreaSourceDensityForced() {
        static const bool forced = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return __system_property_get("debug.massif.areasourcedensity", property) > 0 && property[0] == '1';
        }();
        return forced;
    }
#else
    static bool isLineSourceDensityForced() {
        return false;
    }

    static bool isAreaSourceDensityForced() {
        return false;
    }
#endif


    TileLayer::~TileLayer() {
    }
    
    std::shared_ptr<TileDataSource> TileLayer::getDataSource() const {
        return _dataSource.get();
    }

    std::shared_ptr<Projection> TileLayer::getProjection() const {
        return _dataSource ? _dataSource->getProjection() : std::shared_ptr<Projection>();
    }
    
    std::shared_ptr<TileDataSource> TileLayer::getUTFGridDataSource() const {
        return _utfGridDataSource.get();
    }
    
    void TileLayer::setUTFGridDataSource(const std::shared_ptr<TileDataSource>& dataSource) {
        _utfGridDataSource.set(dataSource);
    }

    int TileLayer::getFrameNr() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _frameNr;
    }
    
    void TileLayer::setFrameNr(int frameNr) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _lastFrameNr = _frameNr;
            _frameNr = frameNr;
        }
        refresh();
    }
    
    bool TileLayer::isPreloading() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _preloading;
    }
    
    void TileLayer::setPreloading(bool preloading) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _preloading = preloading;
    }
    
    bool TileLayer::isSynchronizedRefresh() const {
        return _synchronizedRefresh.load();
    }
    
    void TileLayer::setSynchronizedRefresh(bool synchronizedRefresh) {
        _synchronizedRefresh.store(synchronizedRefresh);
    }
    
    TileSubstitutionPolicy::TileSubstitutionPolicy TileLayer::getTileSubstitutionPolicy() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _substitutionPolicy;
    }
    
    void TileLayer::setTileSubstitutionPolicy(TileSubstitutionPolicy::TileSubstitutionPolicy policy) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _substitutionPolicy = policy;
        }
        refresh();
    }
        
    float TileLayer::getZoomLevelBias() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _zoomLevelBias;
    }
    
    void TileLayer::setZoomLevelBias(float bias) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _zoomLevelBias = bias;
        }
        refresh();
    }
    
    int TileLayer::getMaxOverzoomLevel() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _maxOverzoomLevel;
    }
    
    void TileLayer::setMaxOverzoomLevel(int overzoomLevel) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _maxOverzoomLevel = overzoomLevel;
        }
        refresh();
    }

    int TileLayer::getMaxStandInLevel() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _maxStandInLevel;
    }

    void TileLayer::setMaxStandInLevel(int standInLevel) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _maxStandInLevel = standInLevel;
        }
        refresh();
    }
    
    int TileLayer::getMaxUnderzoomLevel() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _maxUnderzoomLevel;
    }
    
    void TileLayer::setMaxUnderzoomLevel(int underzoomLevel) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _maxUnderzoomLevel = underzoomLevel;
        }
        refresh();
    }
    
    MapTile TileLayer::calculateMapTile(const MapPos& mapPos, int zoom) const {
        return TileUtils::CalculateMapTile(mapPos, zoom, _dataSource->getProjection());
    }
    
    MapPos TileLayer::calculateMapTileOrigin(const MapTile& mapTile) const {
        return TileUtils::CalculateMapTileOrigin(mapTile, _dataSource->getProjection());
    }

    MapBounds TileLayer::calculateMapTileBounds(const MapTile& mapTile) const {
        return TileUtils::CalculateMapTileBounds(mapTile, _dataSource->getProjection());
    }
    
    void TileLayer::clearTileCaches(bool all) {
        clearTiles(true);
        if (all) {
            clearTiles(false);
        }
    }

    std::shared_ptr<TileLoadListener> TileLayer::getTileLoadListener() const {
        return _tileLoadListener.get();
    }
    
    void TileLayer::setTileLoadListener(const std::shared_ptr<TileLoadListener>& tileLoadListener) {
        _tileLoadListener.set(tileLoadListener);
    }

    std::shared_ptr<UTFGridEventListener> TileLayer::getUTFGridEventListener() const {
        return _utfGridEventListener.get();
    }
    
    void TileLayer::setUTFGridEventListener(const std::shared_ptr<UTFGridEventListener>& utfGridEventListener) {
        _utfGridEventListener.set(utfGridEventListener);
    }
    
    bool TileLayer::isUpdateInProgress() const {
        return !_fetchingTileTasks.getAll().empty();
    }
    
    TileLayer::DataSourceListener::DataSourceListener(const std::shared_ptr<TileLayer>& layer) :
        _layer(layer)
    {
    }
        
    void TileLayer::DataSourceListener::onTilesChanged(bool removeTiles) {
        if (std::shared_ptr<TileLayer> layer = _layer.lock()) {
            layer->updateTiles(removeTiles);
        } else {
            Log::Error("TileLayer::DataSourceListener: Lost connection to layer");
        }
    }
        
    TileLayer::TileLayer(const std::shared_ptr<TileDataSource>& dataSource) :
        Layer(),
        _dataSource(dataSource),
        _dataSourceListener(),
        _tileRenderer(std::make_shared<TileRenderer>()),
        _fetchingTileTasks(),
        _calculatingTiles(false),
        _refreshedTiles(false),
        _utfGridDataSource(),
        _tileLoadListener(),
        _utfGridEventListener(),
        _synchronizedRefresh(false),
        _frameNr(0),
        _lastFrameNr(0),
        _preloading(false),
        _substitutionPolicy(TileSubstitutionPolicy::TILE_SUBSTITUTION_POLICY_ALL),
        _zoomLevelBias(0.0f),
        _maxOverzoomLevel(MAX_PARENT_SEARCH_DEPTH),
        _maxStandInLevel(MAX_STAND_IN_DEPTH),
        _maxUnderzoomLevel(MAX_CHILD_SEARCH_DEPTH),
        _visibleTiles(),
        _preloadingTiles(),
        _utfGridTiles(),
        _tileCullState(),
        _glResourceManager(),
        _projectionSurface()
    {
        if (!dataSource) {
            throw NullArgumentException("Null dataSource");
        }

        if (!std::dynamic_pointer_cast<EPSG3857>(dataSource->getProjection())) {
            throw InvalidArgumentException("Expecting EPSG3857 projection in datasource");
        }

        resetTileTransformer();
    }
    
    void TileLayer::setComponents(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool,
                                  const std::shared_ptr<CancelableThreadPool>& tileThreadPool,
                                  const std::weak_ptr<Options>& options,
                                  const std::weak_ptr<MapRenderer>& mapRenderer,
                                  const std::weak_ptr<TouchHandler>& touchHandler)
    {
        Layer::setComponents(envelopeThreadPool, tileThreadPool, options, mapRenderer, touchHandler);
        _tileRenderer->setComponents(options, mapRenderer);

        // To reduce memory usage, release all the caches now
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        clearTileCaches(true);
        _projectionSurface.reset();
        _glResourceManager.reset();
    }

    void TileLayer::loadData(const std::shared_ptr<CullState>& cullState) {
        // This method (from update() or refresh()) might be called from multiple threads
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        _calculatingTiles = true;

        // Check if we need to invalidate caches
        std::shared_ptr<ProjectionSurface> projectionSurface;
        std::shared_ptr<GLResourceManager> glResourceManager;
        if (auto mapRenderer = getMapRenderer()) {
            projectionSurface = mapRenderer->getProjectionSurface();
            glResourceManager = mapRenderer->getGLResourceManager();
        }
        if (_projectionSurface.lock() != projectionSurface || _glResourceManager.lock() != glResourceManager) {
            clearTileCaches(true);
            resetTileTransformer();
            _projectionSurface = projectionSurface;
            _glResourceManager = glResourceManager;
        }

        // Check if the terrain configuration has changed. Any change requires tiles to be rebuilt.
        {
            std::shared_ptr<TerrainOptions> terrainOptions;
            if (auto options = getOptions()) {
                terrainOptions = options->getTerrainOptions();
            }
            bool terrainEnabled = terrainOptions && terrainOptions->isEnabled();
            int terrainMeshResolution = terrainOptions ? terrainOptions->getMeshResolution() : 0;
            int terrainMinZoom = terrainOptions ? terrainOptions->getMinZoom() : 0;
            // Fills stay subdivided even under draping, because draping is decided per tile at
            // render time and this density globally at decode time - see docs/internals/rendering/02-tiles.md.
            // MUST match what resetTileTransformer() passes, or tiles decoded for the other mode
            // stay in the cache forever.
            bool terrainTangramContent = terrainEnabled && terrainOptions && !terrainOptions->isDrapeFillsEnabled();
            bool terrainSourceDensity = isAreaSourceDensityForced();
            bool terrainSourceDensityLines = terrainTangramContent || (terrainOptions && terrainOptions->isDrapeLinesEnabled()) || isLineSourceDensityForced();
            // NOT the exaggeration: only the GPU reads it (via the elevation texture's
            // metersToInternal), so comparing it here re-decoded the whole map on every 'expand' frame.
            if (_terrainOptions.lock() != terrainOptions || _terrainEnabled != terrainEnabled || _terrainMeshResolution != terrainMeshResolution || _terrainMinZoom != terrainMinZoom || _terrainSourceDensity != terrainSourceDensity || _terrainSourceDensityLines != terrainSourceDensityLines) {
                clearTileCaches(true);
                resetTileTransformer();
                _terrainOptions = terrainOptions;
                _terrainEnabled = terrainEnabled;
                _terrainMeshResolution = terrainMeshResolution;
                _terrainMinZoom = terrainMinZoom;
                _terrainSourceDensity = terrainSourceDensity;
                _terrainSourceDensityLines = terrainSourceDensityLines;
            }
        }

        // Remove UTF grid tiles that are missing from the cache
        for (auto it = _utfGridTiles.begin(); it != _utfGridTiles.end(); ) {
            long long tileId = getTileId(it->first);
            if (!tileExists(tileId, false) && !tileExists(tileId, true)) {
                it = _utfGridTiles.erase(it);
            } else {
                it++;
            }
        }
    
        // Check if layer should be drawn
        if (!isVisible() || !getVisibleZoomRange().inRange(cullState->getViewState().getZoom()) || getOpacity() <= 0) {
            _calculatingTiles = false;
            VT_STAT_INC(tileLayersSkipped);

            // Report the real change, not an unconditional one. This path runs on every cull
            // pass for as long as the layer stays hidden, and a hardcoded 'changed' makes a
            // VectorTileLayer request a redraw and a full label placement pass every time -
            // and the redraw brings the cull worker straight back here. A hidden layer then
            // burns a placement pass over every OTHER layer's labels, several times a second,
            // on a completely still map. refreshTiles below still reports true once, on the
            // pass that empties the tile set, which is the only change there is to report.
            refreshDrawData(cullState, false);
            return;
        }

        // The view distance and the LOD threshold decide which tiles are visible, but neither is
        // part of the view matrix: changing one on a still map would otherwise only take effect the
        // next time the camera moves (the option looks dead, then the ground suddenly ends mid-pan).
        {
            float viewDistanceFactor = 0.0f;
            float lodFactor = 0.0f;
            int coarsening = 0;
            // Auto-flattening switches the terrain LOD and the overzoom targets off without touching
            // the decoded tiles, so it belongs here and not in the cache check above.
            bool terrainActive = false;
            if (auto options = getOptions()) {
                lodFactor = options->getTileLODFactor();
                if (auto terrainOptions = options->getTerrainOptions()) {
                    viewDistanceFactor = terrainOptions->getViewDistanceFactor();
                    coarsening = terrainOptions->getMaxTileZoomCoarsening();
                    terrainActive = terrainOptions->isActive();
                }
            }
            if (_terrainViewDistanceFactor != viewDistanceFactor || _tileLODFactor != lodFactor || _terrainCoarsening != coarsening || _terrainActive != terrainActive) {
                _terrainViewDistanceFactor = viewDistanceFactor;
                _tileLODFactor = lodFactor;
                _terrainCoarsening = coarsening;
                _terrainActive = terrainActive;
                _tileCullState.reset(); // re-cull with the new rule, tiles themselves stay valid
            }
        }

        // Check if tiles need to be recalculated
        bool recalculateTiles = (!_tileCullState || _frameNr != _lastFrameNr || cullState->getViewState().getModelviewProjectionMat() != _tileCullState->getViewState().getModelviewProjectionMat());
        if (recalculateTiles) {
            VT_STAT_INC(tileRecalculations);
            // If the view has changed calculate new visible tiles, otherwise use the old ones
            calculateVisibleTiles(cullState);

            _tileCullState = cullState;
        }
    
        // Find replacements for visible tiles, create fetch list
        std::vector<FetchTileInfo> fetchTileList;
        buildFetchTiles(_visibleTiles, false, fetchTileList);
        if (_preloading) {
            buildFetchTiles(_preloadingTiles, true, fetchTileList);
        }

        // If there are multiple missing visible tiles with shared parent, then fetch the parent tile to provide quick rendering
        std::unordered_map<MapTile, int> childTileCountMap;
        for (const FetchTileInfo& fetchTile : fetchTileList) {
            if (!fetchTile.preloading && fetchTile.tile.getZoom() > 0) {
                childTileCountMap[fetchTile.tile.getParent()]++;
            }
        }
        for (std::pair<MapTile, int> childTileCount : childTileCountMap) {
            if (childTileCount.second > 1) {
                long long tileId = getTileId(childTileCount.first);
                if (!prefetchTile(tileId, false)) {
                    fetchTileList.push_back({ childTileCount.first, false, PARENT_PRIORITY_OFFSET });
                }
            }
        }

        // Sort the fetch tile list.
        // The sorting order is based on priority delta, preloading flag and finally on whether the tile has parents in the fetch list.
        std::stable_sort(fetchTileList.begin(), fetchTileList.end(), [&childTileCountMap](const FetchTileInfo& fetchTile1, const FetchTileInfo& fetchTile2) {
            if (fetchTile1.priorityDelta != fetchTile2.priorityDelta) {
                return fetchTile1.priorityDelta > fetchTile2.priorityDelta;
            }
            if (fetchTile1.preloading != fetchTile2.preloading) {
                return fetchTile1.preloading < fetchTile2.preloading;
            }
            return (childTileCountMap[fetchTile1.tile.getParent()] > 1) < (childTileCountMap[fetchTile2.tile.getParent()] > 1);
        });

        // Fetch the tiles
        std::unordered_set<long long> fetchedTiles;
        for (const FetchTileInfo& fetchTileInfo : fetchTileList) {
            long long tileId = getTileId(fetchTileInfo.tile);
            if (fetchedTiles.find(tileId) != fetchedTiles.end()) {
                continue;
            }
            fetchedTiles.insert(tileId);

            // If there is an existing task for this tile, keep it. Otherwise fetch it.
            bool found = false;
            for (std::shared_ptr<FetchTaskBase> task : _fetchingTileTasks.get(tileId)) {
                if (!task->isCanceled()) {
                    if (task->isPreloadingTile() == fetchTileInfo.preloading) {
                        found = true;
                        break;
                    }
                    task->cancel();
                }
            }
            if (!found) {
                fetchTile(getTileId(fetchTileInfo.tile), fetchTileInfo.tile, fetchTileInfo.preloading, fetchTileInfo.priorityDelta);
            }
        }

        // Cancel old tasks
        for (const std::shared_ptr<FetchTaskBase>& task : _fetchingTileTasks.getAll()) {
            if (fetchedTiles.find(task->getTileId()) == fetchedTiles.end()) {
                task->cancel();
            }
        }
    
        // Done. Refresh.
        _calculatingTiles = false;
        _refreshedTiles = true;
        
        refreshDrawData(cullState, recalculateTiles);
    }

    void TileLayer::updateTiles(bool removeTiles) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            // Reset cullstate. This will force recalculation of visible tiles, which is important if data extent has changed.
            _tileCullState.reset();

            // Invalidate current tasks
            for (const std::shared_ptr<FetchTaskBase>& task : _fetchingTileTasks.getAll()) {
                task->invalidate();
                task->cancel();
            }

            // Flush caches
            if (removeTiles) {
                clearTiles(false);
                clearTiles(true);
            } else {
                invalidateTiles(false);
                clearTiles(true);
            }
        }
        refresh();
    }

    void TileLayer::updateTileLoadListener() {
        bool calculatingTiles = _calculatingTiles;
    
        DirectorPtr<TileLoadListener> tileLoadListener = _tileLoadListener;

        if (!calculatingTiles && tileLoadListener) {
            bool refreshedTiles = std::atomic_exchange(&_refreshedTiles, false);
    
            // Check if visible tiles have finished loading, notify listener
            if (refreshedTiles && _fetchingTileTasks.getVisibleCount() == 0) {
                tileLoadListener->onVisibleTilesLoaded();
            }
    
            // Check if preloading tiles have finished loading, notify listener
            if (isPreloading() && refreshedTiles && _fetchingTileTasks.getPreloadingCount() == 0) {
                tileLoadListener->onPreloadingTilesLoaded();
            }
        }
    }
    
    void TileLayer::calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const {
        DirectorPtr<TileDataSource> utfGridDataSource = _utfGridDataSource;

        if (!utfGridDataSource) {
            return;
        }

        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface) {
            return;
        }

        // Find intersection with projection surface
        double t = -1;
        if (!projectionSurface->calculateHitPoint(ray, 0, t) || t < 0) {
            return;
        }

        MapPos mapPos = utfGridDataSource->getProjection()->fromInternal(projectionSurface->calculateMapPos(ray(t)));
        int zoom = std::min(getMaxZoom(), static_cast<int>(viewState.getZoom() + getZoomLevelBias() + DISCRETE_ZOOM_LEVEL_BIAS));

        // Try to get the tile from cache
        std::shared_ptr<UTFGridTile> utfGridTile;
        int utfGridTileZoom = -1;
        for (MapTile flippedTile = calculateMapTile(mapPos, utfGridDataSource->getMaxZoom()).getFlipped(); true; flippedTile = flippedTile.getParent()) {
            if (std::abs(flippedTile.getZoom() - zoom) < std::abs(utfGridTileZoom - zoom)) {
                long long flippedTileId = getTileId(flippedTile);
                if (tileExists(flippedTileId, false) || tileExists(flippedTileId, true)) {
                    std::lock_guard<std::recursive_mutex> lock(_mutex);
                    auto it = _utfGridTiles.find(flippedTile);
                    if (it != _utfGridTiles.end()) {
                        utfGridTile = it->second;
                        utfGridTileZoom = flippedTile.getZoom();
                    }
                }
            }
            if (flippedTile.getZoom() == 0) {
                break;
            }
        }

        // If succeeded and valid key under the click position, call the listener
        if (utfGridTile) {
            MapTile mapTile = calculateMapTile(mapPos, std::min(utfGridDataSource->getMaxZoom(), std::max(utfGridDataSource->getMinZoom(), utfGridTileZoom)));
            double tileWidth = utfGridDataSource->getProjection()->getBounds().getDelta().getX() / (1 << mapTile.getZoom());
            double tileHeight = utfGridDataSource->getProjection()->getBounds().getDelta().getY() / (1 << mapTile.getZoom());

            MapVec mapVec(mapTile.getX() * tileWidth, mapTile.getY() * tileHeight);
            MapPos mapTileOrigin = utfGridDataSource->getProjection()->getBounds().getMin() + mapVec;
            double xRel = (mapPos.getX() - mapTileOrigin.getX()) / tileWidth;
            double yRel = 1 - (mapPos.getY() - mapTileOrigin.getY()) / tileHeight;
            int x = static_cast<int>(std::floor(xRel * utfGridTile->getXSize()));
            int y = static_cast<int>(std::floor(yRel * utfGridTile->getYSize()));
            int keyId = utfGridTile->getKeyId(x, y);
            if (keyId != 0) {
                auto elementInfo = std::make_shared<Variant>(utfGridTile->getData(utfGridTile->getKey(keyId)));
                std::shared_ptr<Layer> thisLayer = std::const_pointer_cast<Layer>(shared_from_this());
                results.push_back(RayIntersectedElement(elementInfo, thisLayer, ray(t), ray(t), false));
            }
        }
    }

    bool TileLayer::processClick(const ClickInfo& clickInfo, const RayIntersectedElement& intersectedElement, const ViewState& viewState) const {
        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface) {
            return false;
        }
        
        DirectorPtr<UTFGridEventListener> utfGridEventListener = _utfGridEventListener;

        if (utfGridEventListener) {
            if (auto elementInfo = intersectedElement.getElement<Variant>()) {
                MapPos hitPos = _dataSource->getProjection()->fromInternal(projectionSurface->calculateMapPos(intersectedElement.getHitPos()));
                auto utfClickInfo = std::make_shared<UTFGridClickInfo>(clickInfo, hitPos, *elementInfo, intersectedElement.getLayer());
                return utfGridEventListener->onUTFGridClicked(utfClickInfo);
            }
        }

        return clickInfo.getClickType() == ClickType::CLICK_TYPE_SINGLE || clickInfo.getClickType() == ClickType::CLICK_TYPE_LONG; // by default, disable 'click through' for single and long clicks
    }

    void TileLayer::calculateVisibleTiles(const std::shared_ptr<CullState>& cullState) {
        // Remove last visible and preloading tiles
        _visibleTiles.clear();
        _preloadingTiles.clear();

        // In terrain mode the distance-based LOD picks higher-zoom tiles near the camera
        // than flat rendering would show at the same camera zoom; if the style renders
        // differently at different tile zooms, the LOD rings become visible as patches.
        // TerrainOptions::setMaxTileZoomOffset caps the tile detail relative to what flat
        // rendering would use.
        _terrainMaxTileZoom = 1000;
        _terrainMinTileZoom = 0;
        _terrainOverzoomTargets = false;
        if (auto options = getOptions()) {
            if (auto terrainOptions = options->getTerrainOptions()) {
                if (terrainOptions->isActive()) {
                    // Terrain mode: allow target tiles BEYOND the data source maximum
                    // zoom (fed from ancestor tiles by the regular overzoom machinery).
                    // The tile surfaces are the terrain depth occluders and their
                    // tesselation is proportional to the tile size - capping targets at
                    // the data source maximum (e.g. a z12 DEM-derived hillshade under a
                    // z15 camera) leaves cells many times coarser than the base map's,
                    // and the resulting blunted ridges are leaky occluders that content
                    // and vector elements show through near crests.
                    _terrainOverzoomTargets = true;
                    const ViewState& viewState = cullState->getViewState();
                    int cameraTileZoom = static_cast<int>(viewState.getZoom() + getZoomLevelBias() + DISCRETE_ZOOM_LEVEL_BIAS);
                    if (terrainOptions->getMaxTileZoomOffset() < 100) {
                        _terrainMaxTileZoom = cameraTileZoom + terrainOptions->getMaxTileZoomOffset();
                    }
                    _terrainMinTileZoom = cameraTileZoom - terrainOptions->getMaxTileZoomCoarsening();
                }
            }
        }

        // How far the map is drawn: tangram's view distance (ViewState::calculateViewDistance), which
        // the style may EXTEND with an absolute one in metres (and may make it depend on the zoom).
        // Looking along the ground the camera sees to the horizon, which is hundreds of tiles, almost
        // all of them a few pixels tall and each carrying its own labels; this is what keeps that view
        // affordable. Pair a short one with fog, or the ground simply ends.
        _maxVisibleDistance = 0;
        {
            StyleEnvironment env;
            if (auto options = getOptions()) {
                _maxVisibleDistance = cullState->getViewState().calculateViewDistance(*options);
            }
            if (getStyleEnvironment(cullState->getViewState(), env) && env.terrainMaxVisibleDistance && *env.terrainMaxVisibleDistance > 0) {
                _maxVisibleDistance = std::max(_maxVisibleDistance, *env.terrainMaxVisibleDistance * static_cast<double>(Const::WORLD_SIZE) / Const::EARTH_CIRCUMFERENCE);
            }
        }

        // A coarsening floor and a view distance are set independently and multiply into the tile
        // count; relax the floor rather than shorten the view (docs/internals/rendering/02-tiles.md).
        if (_terrainMinTileZoom > 0 && _maxVisibleDistance > 0) {
            // Tiles across the covered ground, worst case (a square of side 2 * distance):
            //     (2 * distance / tileWidth)^2 <= budget,   tileWidth = WORLD_SIZE / 2^zoom
            double maxTileZoom = std::log2(Const::WORLD_SIZE * std::sqrt(static_cast<double>(TERRAIN_COVER_TILE_BUDGET)) / (2 * _maxVisibleDistance));
            int budgetMinTileZoom = static_cast<int>(std::floor(maxTileZoom));
            if (budgetMinTileZoom < _terrainMinTileZoom) {
                if (_terrainMinTileZoom - budgetMinTileZoom > 1) {
                    Log::Infof("TileLayer::calculateVisibleTiles: the view distance needs %d more levels of terrain tile coarsening than allowed; coarsening to zoom %d to stay inside %d tiles",
                               _terrainMinTileZoom - budgetMinTileZoom, budgetMinTileZoom, TERRAIN_COVER_TILE_BUDGET);
                }
                _terrainMinTileZoom = std::max(0, budgetMinTileZoom);
            }
        }

        // Elevation the LOD projects tile corners at. Per tile where the DEM is decoded, screen-centre
        // elevation elsewhere - tangram's View::getTileScreenArea uses the screen centre for every tile
        // (docs/internals/rendering/02-tiles.md, the LOD rule).
        _lodElevation = 0;
        _lodElevationManager.reset();
        if (auto options = getOptions()) {
            if (auto terrainOptions = options->getTerrainOptions()) {
                if (terrainOptions->isActive()) {
                    if (auto elevationManager = terrainOptions->getElevationManager()) {
                        _lodElevationManager = elevationManager;
                        const cglib::vec3<double>& focusPos = cullState->getViewState().getFocusPos();
                        _lodElevation = elevationManager->getDisplayHeight(focusPos(0), focusPos(1), ElevationManager::LoadMode::CACHED_ONLY);
                    }
                }
            }
        }

        _lodMaxTileArea = 0;
        _lodMinCosTheta = 0;
        if (auto options = getOptions()) {
            const ViewState& viewState = cullState->getViewState();
            double tileSizePixels = options->getTileDrawSize() * viewState.getDPI() / Const::UNSCALED_DPI;
            // Options::TileLODFactor scales it: 1 is their rule verbatim, larger keeps tiles
            // coarser (fewer tiles, fewer labels, less detail), smaller refines further.
            double maxEdge = 2.0 * tileSizePixels * std::max(0.0f, options->getTileLODFactor());
            // A source whose tiles are bigger than the nominal size carries a zoom bias; the same
            // bias applies to the area it is allowed to cover (tangram: maxArea * exp2(2*zoomBias)).
            _lodMaxTileArea = maxEdge * maxEdge * std::pow(4.0, -getZoomLevelBias());
            // Floor on cos(incidence): the area falls with the distance AND with the grazing angle,
            // and only the second term is bounded here (docs/internals/rendering/02-tiles.md).
            float limit = options->getTileLODForeshorteningLimit();
            _lodMinCosTheta = limit > 0 ? std::pow(2.0, -2.0 * limit) : 0.0;
        }

        // Recursively calculate visible tiles
        calculateVisibleTilesRecursive(cullState, MapTile(0, 0, 0, _frameNr), _dataSource->getDataExtent());
        if (auto options = getOptions()) {
            if (options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR && options->isSeamlessPanning()) {
                // Additional visibility testing has to be done if seamless panning is enabled
                for (int i = 1; i <= 5; i++) {
                    calculateVisibleTilesRecursive(cullState, MapTile(-i, 0, 0, _frameNr), _dataSource->getDataExtent());
                    calculateVisibleTilesRecursive(cullState, MapTile( i, 0, 0, _frameNr), _dataSource->getDataExtent());
                }
            }
        }
        
        sortTiles(_visibleTiles, cullState->getViewState(), false);
        sortTiles(_preloadingTiles, cullState->getViewState(), true);
    }

    void TileLayer::calculateVisibleTilesRecursive(const std::shared_ptr<CullState>& cullState, const MapTile& tile, const MapBounds& dataExtent) {
        const ViewState& viewState = cullState->getViewState();
        const cglib::frustum3<double>& visibleFrustum = viewState.getFrustum();
        
        if (tile.getZoom() > Const::MAX_SUPPORTED_ZOOM_LEVEL) {
            return;
        }

        int tileMask = (1 << tile.getZoom()) - 1;
        MapTile flippedTile(tile.getX() & tileMask, tileMask - (tile.getY() & tileMask), tile.getZoom(), 0);
        if (!calculateMapTileBounds(flippedTile).intersects(dataExtent)) {
            return;
        }
        
        std::shared_ptr<vt::TileTransformer> tileTransformer = getTileTransformer();
        vt::TileId vtTileId(tile.getZoom(), tile.getX(), tile.getY());
        cglib::bbox3<double> tileBounds = tileTransformer->calculateTileBBox(vtTileId);
        cglib::vec3<double> tileCenter = tileBounds.center();
        cglib::bbox3<double> preloadingBounds(tileCenter + (tileBounds.min - tileCenter) * PRELOADING_TILE_SCALE, tileCenter + (tileBounds.max - tileCenter) * PRELOADING_TILE_SCALE);

        bool inPreloadingFrustum = visibleFrustum.inside(preloadingBounds);
        if (!inPreloadingFrustum) {
            return;
        }
        // Beyond the view distance nothing is drawn, so nothing is fetched either - and the
        // recursion stops here rather than subdividing a tile that will never be seen. The test
        // is against the NEAREST point of the tile, so a tile straddling the limit still counts.
        if (_maxVisibleDistance > 0) {
            const cglib::vec3<double>& cameraPos = viewState.getCameraPos();
            cglib::vec3<double> nearestPos = cameraPos;
            for (int i = 0; i < 3; i++) {
                nearestPos(i) = std::max(tileBounds.min(i), std::min(tileBounds.max(i), cameraPos(i)));
            }
            if (cglib::length(nearestPos - cameraPos) > _maxVisibleDistance) {
                return;
            }
        }
        bool inVisibleFrustum = visibleFrustum.inside(tileBounds);

        // Tangram's rule (View::getTileScreenArea): project the four corners at surface level and
        // compare the area they enclose. A tile crossing the camera plane has no meaningful
        // projected area and is always subdivided.
        const cglib::mat4x4<double>& mvpMat = viewState.getModelviewProjectionMat();
        cglib::mat4x4<double> tileMat = tileTransformer->calculateTileMatrix(vtTileId, 1.0f);
        // The height THIS tile sits at, not the height under the screen centre that tangram uses for
        // every tile - at a low tilt the focus can be a kilometre above the near ground. The band
        // midpoint, not its top: see docs/internals/rendering/02-tiles.md, the LOD rule.
        double lodElevation = _lodElevation;
        if (_lodElevationManager) {
            double minZ = 0, maxZ = 0;
            if (_lodElevationManager->getMinMaxDisplayHeightCached(MapTile(tile.getX() & tileMask, tile.getY(), tile.getZoom(), 0), minZ, maxZ)) {
                lodElevation = (minZ + maxZ) * 0.5;
            }
        }
        double screenArea = std::numeric_limits<double>::infinity();
        {
            static const cglib::vec3<double> CORNERS[4] = {
                cglib::vec3<double>(0, 0, 0), cglib::vec3<double>(1, 0, 0),
                cglib::vec3<double>(1, 1, 0), cglib::vec3<double>(0, 1, 0)
            };
            cglib::vec2<double> screenPos[4];
            bool projected = true;
            for (int i = 0; i < 4; i++) {
                cglib::vec3<double> worldPos = cglib::transform_point(CORNERS[i], tileMat);
                worldPos(2) += lodElevation;
                cglib::vec4<double> clipPos = cglib::transform(cglib::vec4<double>(worldPos(0), worldPos(1), worldPos(2), 1.0), mvpMat);
                if (!(clipPos(3) > 0)) {
                    projected = false;
                    break;
                }
                screenPos[i] = cglib::vec2<double>(clipPos(0) / clipPos(3) * viewState.getHalfWidth(), clipPos(1) / clipPos(3) * viewState.getHalfHeight());
            }
            if (projected) {
                double area = 0;
                for (int i = 0; i < 4; i++) {
                    const cglib::vec2<double>& p = screenPos[i];
                    const cglib::vec2<double>& q = screenPos[(i + 1) % 4];
                    area += p(0) * q(1) - q(0) * p(1);
                }
                screenArea = std::abs(area) * 0.5;
                if (_lodMinCosTheta > 0) {
                    cglib::vec3<double> toTile = tileCenter + cglib::vec3<double>(0, 0, lodElevation) - viewState.getCameraPos();
                    double dist = cglib::length(toTile);
                    double cosTheta = dist > 0 ? std::abs(toTile(2)) / dist : 1.0;
                    if (cosTheta > 0 && cosTheta < _lodMinCosTheta) {
                        screenArea *= _lodMinCosTheta / cosTheta; // the area this tile would have at the limit
                    }
                }
            }
        }
        bool subDivide = !(_lodMaxTileArea > 0) || screenArea >= _lodMaxTileArea;
        // TERRAIN: the tile surface is the depth OCCLUDER, and its tesselation is proportional to
        // the tile size. Let a tile coarsen freely and its ridge crests are chopped flat, so
        // content drawn over a finer tile of another layer - a road, a contour - shows through the
        // ridge in front of it. Layers also coarsen independently (each runs the area test with its
        // own tile size and zoom bias), so the occluder can end up coarser than the content it is
        // meant to hide. Bounding how far BELOW the camera zoom a tile may sit bounds that
        // mismatch, and costs only the horizon band, where the area rule would otherwise drop 5 or
        // 6 levels at once.
        if (_terrainMinTileZoom > 0 && tile.getZoom() < _terrainMinTileZoom) {
            subDivide = true;
        }
        int maxTargetZoom = getMaxZoom() + (_terrainOverzoomTargets ? getMaxOverzoomLevel() : 0);
        int targetTileZoom = std::min(maxTargetZoom, static_cast<int>(viewState.getZoom() + getZoomLevelBias() + DISCRETE_ZOOM_LEVEL_BIAS));
        targetTileZoom = std::min(targetTileZoom, _terrainMaxTileZoom);
        if (getMinZoom() > tile.getZoom()) {
            subDivide = true;
        } else if (targetTileZoom <= tile.getZoom()) {
            subDivide = false;
        }
        
        if (subDivide) {
            // The tile is too coarse, keep subdividing
            for (int n = 0; n < 4; n++) {
                calculateVisibleTilesRecursive(cullState, tile.getChild(n), dataExtent);
            }
        } else {
            // Add the tile to visible tiles, sort by the distnace to the camera
            if (inVisibleFrustum) {
                _visibleTiles.push_back(tile);
            } else {
                _preloadingTiles.push_back(tile);
            }
        }
    }
    
    void TileLayer::sortTiles(std::vector<MapTile>& tiles, const ViewState& viewState, bool preloadingTiles) {
        typedef std::pair<std::tuple<int, int, double>, MapTile> TaggedMapTile;

        // Create tagged tile list. Store parent/child substitution level and distance from camera center
        std::vector<TaggedMapTile> taggedTiles;
        taggedTiles.reserve(tiles.size());
        for (const MapTile& mapTile : tiles) {
            int parentSubstLevel = 0;
            long long parentTileId = getTileId(mapTile.getParent());
            if (tileExists(parentTileId, preloadingTiles) || tileExists(parentTileId, !preloadingTiles)) {
                parentSubstLevel = 1;
            }
            int childSubstLevel = 0;
            for (int n = 0; n < 4; n++) {
                long long subTileId = getTileId(mapTile.getChild(n));
                if (tileExists(subTileId, preloadingTiles) || tileExists(subTileId, !preloadingTiles)) {
                    childSubstLevel = 1;
                    break;
                }
            }

            cglib::vec3<double> center = getTileTransformer()->calculateTileOrigin(vt::TileId(mapTile.getZoom(), mapTile.getX(), mapTile.getY()));
            double dist = cglib::length(center - viewState.getCameraPos());

            taggedTiles.emplace_back(std::make_tuple(parentSubstLevel, childSubstLevel, dist), mapTile);
        }

        // Sort tiles
        std::sort(taggedTiles.begin(), taggedTiles.end(), [](const TaggedMapTile& tile1, const TaggedMapTile& tile2) {
            return tile1.first < tile2.first;
        });

        // Copy sorted tiles back
        std::transform(taggedTiles.begin(), taggedTiles.end(), tiles.begin(), [](const TaggedMapTile& tile) {
            return tile.second;
        });
    }
    
    void TileLayer::buildFetchTiles(const std::vector<MapTile>& visTiles, bool preloadingTiles, std::vector<FetchTileInfo>& fetchTileList) {
        for (const MapTile& visTile : visTiles) {
            int tileMask = (1 << visTile.getZoom()) - 1;
            MapTile tile(visTile.getX() & tileMask, visTile.getY() & tileMask, visTile.getZoom(), visTile.getFrameNr());
            long long tileId = getTileId(tile);

            // Check caches
            if (tileExists(tileId, preloadingTiles) || tileExists(tileId, !preloadingTiles)) {
                calculateDrawData(visTile, tile, preloadingTiles);

                // Re-fetch invalid tile
                if (!tileValid(tileId, preloadingTiles) && !tileValid(tileId, !preloadingTiles)) {
                    fetchTileList.push_back({ tile, preloadingTiles, (preloadingTiles ? PRELOADING_PRIORITY_OFFSET : 0) });
                }
                continue;
            }
            
            // Build list of caches to use (based on tile substitution policy)
            std::vector<bool> preloadingCaches;
            switch (getTileSubstitutionPolicy()) {
            case TileSubstitutionPolicy::TILE_SUBSTITUTION_POLICY_ALL:
                preloadingCaches.push_back(false);
                preloadingCaches.push_back(true);
                break;
            case TileSubstitutionPolicy::TILE_SUBSTITUTION_POLICY_VISIBLE:
                preloadingCaches.push_back(false);
                if (preloadingTiles) {
                    preloadingCaches.push_back(true);
                }
                break;
            default:
                break;
            }
            for (bool preloadingCache : preloadingCaches) {
                // Check for a tile with the last frame nr
                MapTile prevFrameTile(tile.getX(), tile.getY(), tile.getZoom(), _lastFrameNr);
                long prevFrameTileId = getTileId(prevFrameTile);
                bool foundSubstitute = tileExists(prevFrameTileId, preloadingCache);

                if (foundSubstitute) {
                    calculateDrawData(visTile, prevFrameTile, preloadingTiles);
                } else {
                    // Check cache for parent tile
                    if (tile.getZoom() > 0) {
                        foundSubstitute = findParentTile(visTile, tile, getMaxStandInLevel(), preloadingCache, preloadingTiles);
                    }
                    if (!foundSubstitute) {
                        // Didn't find parent tile, check cache for children tiles
                        foundSubstitute = findChildTiles(visTile, tile, getMaxUnderzoomLevel(), preloadingCache, preloadingTiles) > 0;
                    }
                }
                if (foundSubstitute) {
                    break;
                }
            }
    
            // Prefetch, add the tile to the fetch list
            if (!prefetchTile(tileId, preloadingTiles)) {
                fetchTileList.push_back({ tile, preloadingTiles, (preloadingTiles ? PRELOADING_PRIORITY_OFFSET : 0) });
            }
        }
    }
    
    bool TileLayer::findParentTile(const MapTile& visTile, const MapTile& tile, int depth, bool preloadingCache, bool preloadingTile) {
        if (tile.getZoom() <= 0 || depth <= 0) {
            return false;
        }

        MapTile parentTile = tile.getParent();
        long long parentTileId = getTileId(parentTile);
        
        // Check the cache
        if (tileExists(parentTileId, preloadingCache)) {
            calculateDrawData(visTile, parentTile, preloadingTile);
            return true;
        }
    
        // Dind't find the parent in the cache
        return findParentTile(visTile, parentTile, depth - 1, preloadingCache, preloadingTile);
    }
    
    int TileLayer::findChildTiles(const MapTile& visTile, const MapTile& tile, int depth, bool preloadingCache, bool preloadingTile) {
        if (depth <= 0) {
            return 0;
        }
        
        int childTileCount = 0;
        for (int n = 0; n < 4; n++) {
            MapTile subTile = tile.getChild(n);
            long long subTileId = getTileId(subTile);
            if (tileExists(subTileId, preloadingCache)) {
                calculateDrawData(visTile, subTile, preloadingTile);
                childTileCount++;
            } else {
                childTileCount += findChildTiles(visTile, subTile, depth - 1, preloadingCache, preloadingTile);
            }
        }
        return childTileCount;
    }
    
    std::shared_ptr<vt::TileTransformer> TileLayer::getTileTransformer() const {
        return _tileRenderer->getTileTransformer();
    }

    void TileLayer::setTerrainDepthWriteMode(bool enabled) {
        _tileRenderer->setTerrainDepthWriteMode(enabled);
    }

    void TileLayer::collectDrapeLayers(std::vector<std::shared_ptr<TileLayer> >& drapeLayers, const ViewState& viewState) {
        // The same gate the draw path uses (loadData, onDrawFrame). Under a cross-layer drape the
        // bake IS the drawing, so a layer that would not be drawn must not be collected either -
        // otherwise a layer outside its visible zoom range, or at zero opacity, is baked into the
        // terrain texture and shows up as ground that no style asked for.
        if (isVisible() && getVisibleZoomRange().inRange(viewState.getZoom()) && getOpacity() > 0) {
            drapeLayers.push_back(std::static_pointer_cast<TileLayer>(shared_from_this()));
        }
    }

    void TileLayer::setTerrainPaintTiles(const std::vector<vt::TileId>& tileIds) {
        _tileRenderer->setTerrainPaintTiles(tileIds);
    }

    std::size_t TileLayer::drapeStackSignature() const {
        // The contact shadows belong here rather than in the per-tile fingerprint: a drape tile is
        // fingerprinted from the render tiles OF ITS OWN ZOOM, while the shadow it carries can come
        // from a coarser render tile covering it. A tile baked before the extrusions had decoded
        // then kept no shadow and nothing ever asked it to bake again - which is a launch with a
        // warm tile cache showing no contact shadows until a zoom rebuilds the drape.
        std::size_t signature = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(this));
        if (isGroundAOBakeable()) {
            signature ^= 0x9e3779b9;
        }
        return signature;
    }

    bool TileLayer::prepareTerrainDrapeFrame(float deltaSeconds, const ViewState& viewState) {
        return _tileRenderer->prepareFrame(deltaSeconds, viewState);
    }

    void TileLayer::setExternalDrapeTarget(bool enabled) {
        _tileRenderer->setExternalDrapeTarget(enabled);
    }

    void TileLayer::setExternalDrapeTiles(const std::vector<vt::TileId>& tileIds) {
        _tileRenderer->setExternalDrapeTiles(tileIds);
    }

    void TileLayer::setTerrainGroundTiles(const std::vector<vt::TileId>& tileIds, const std::vector<int>& proxyDepths) {
        _tileRenderer->setTerrainGroundTiles(tileIds, proxyDepths);
    }

    void TileLayer::setTerrainLayerOrdinalBase(int base) {
        _tileRenderer->setTerrainLayerOrdinalBase(base);
    }

    int TileLayer::getStyleLayerCount() const {
        return _tileRenderer->getStyleLayerCount();
    }

    int TileLayer::renderTerrainGround(const Color& color) {
        return _tileRenderer->renderTerrainGround(color);
    }

    void TileLayer::collectDrapeTiles(std::map<vt::TileId, std::size_t>& drapeTiles) const {
        _tileRenderer->collectDrapeTiles(drapeTiles);
    }

    int TileLayer::bakeDrapeTile(const vt::TileId& tileId) {
        return _tileRenderer->bakeDrapeTile(tileId);
    }

    int TileLayer::renderDrapedSurface(const vt::TileId& tileId, unsigned int drapeTexture, float uvOffsetX, float uvOffsetY, float uvScale) {
        return _tileRenderer->renderDrapedSurface(tileId, drapeTexture, uvOffsetX, uvOffsetY, uvScale);
    }

    int TileLayer::blitDrapeTexture(unsigned int srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale) {
        return _tileRenderer->blitDrapeTexture(srcTexture, dstOffsetX, dstOffsetY, dstScale, uvOffsetX, uvOffsetY, uvScale);
    }

    int TileLayer::renderDrapedSurfaceFill(const vt::TileId& tileId, const Color& color) {
        return _tileRenderer->renderDrapedSurfaceFill(tileId, color);
    }

    bool TileLayer::calculateShadowViewProj(const std::vector<vt::TileId>& tileIds, const std::vector<vt::TileId>& casterTileIds, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<vt::TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const {
        return _tileRenderer->calculateShadowViewProj(tileIds, casterTileIds, sunDir, tileHeights, minHeight, maxHeight, distanceFactor, cameraDistance, mapSize, cascade, cascadeCount, boxCasterTileIds, depthRangeMeters, texelMeters, lightViewProj);
    }

    float TileLayer::shadowCasterFadeSignature() const {
        return _tileRenderer->shadowCasterFadeSignature();
    }

    int TileLayer::consumeShadowCastersMissingElevation() {
        return _tileRenderer->consumeShadowCastersMissingElevation();
    }

    int TileLayer::renderShadowCasters(const std::vector<vt::TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround) {
        return _tileRenderer->renderShadowCasters(tileIds, lightViewProj, castGround);
    }

    void TileLayer::setTerrainShadowMask(unsigned int texture, float invScreenWidth, float invScreenHeight) {
        if (_tileRenderer) {
            _tileRenderer->setTerrainShadowMask(texture, invScreenWidth, invScreenHeight);
        }
    }

    int TileLayer::renderTerrainShadowMask(const std::vector<vt::TileId>& tileIds) {
        if (_tileRenderer) {
            return _tileRenderer->renderTerrainShadowMask(tileIds);
        }
        return 0;
    }

    bool TileLayer::isGroundAOActive() const {
        return _tileRenderer && _tileRenderer->isGroundAOActive();
    }

    bool TileLayer::isGroundAOBakeable() const {
        return _tileRenderer && _tileRenderer->isGroundAOBakeable();
    }

    void TileLayer::setLabelOcclusionDepth(unsigned int depthTexture, float occluderSize) {
        if (_tileRenderer) {
            _tileRenderer->setLabelOcclusionDepth(depthTexture, occluderSize);
        }
    }

    bool TileLayer::isLabelOcclusionWanted() const {
        return _tileRenderer && _tileRenderer->isLabelOcclusionWanted();
    }

    int TileLayer::renderLabelOcclusionDepth() {
        if (_tileRenderer) {
            return _tileRenderer->renderLabelOcclusionDepth();
        }
        return 0;
    }

    int TileLayer::renderGroundAOMask() {
        if (_tileRenderer) {
            return _tileRenderer->renderGroundAOMask();
        }
        return 0;
    }

    int TileLayer::bakeGroundAOMask(const vt::TileId& tileId) {
        if (_tileRenderer) {
            return _tileRenderer->bakeGroundAOMask(tileId);
        }
        return 0;
    }

    void TileLayer::setTerrainShadowMap(unsigned int texture, int mapSize, int cascades, const std::array<float, 4>& depthBiases, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, 4>& lightViewProjs) {
        _tileRenderer->setTerrainShadowMap(texture, mapSize, cascades, depthBiases, strength, softness, depthTexture, hardwarePCF, normalOffset, sunDir, lightViewProjs);
    }

    void TileLayer::setTerrainSunLighting(const ResolvedLighting& lighting) {
        _tileRenderer->setTerrainSunLighting(lighting);
    }

    void TileLayer::setTerrainRenderOrder(int order) {
        _tileRenderer->setTerrainRenderOrder(order);
    }

    void TileLayer::resetTileTransformer() {
        std::shared_ptr<vt::TileTransformer> tileTransformer;
        if (auto options = getOptions()) {
            if (options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_SPHERICAL) {
                tileTransformer = std::make_shared<vt::SphericalTileTransformer>(static_cast<float>(Const::WORLD_SIZE / Const::PI));
            }
            else if (auto terrainOptions = options->getTerrainOptions()) {
                if (terrainOptions->isEnabled()) {
                    // The source-density flags must match the ones calculateDrawData compares
                    // against: they decide the tesselation the tiles in the cache were built with,
                    // so a mismatch leaves tiles decoded for the other mode in place forever
                    // (un-subdivided fills sagging through the terrain once draping is switched off).
                    // MUST match what calculateDrawData compares against, or tiles decoded for the
                    // other mode stay in the cache forever.
                    bool tangramContent = !terrainOptions->isDrapeFillsEnabled();
                    tileTransformer = std::make_shared<TerrainTileTransformer>(static_cast<float>(Const::WORLD_SIZE), terrainOptions->getElevationManager(), terrainOptions->getMeshResolution(), terrainOptions->getMinZoom(), isAreaSourceDensityForced(), tangramContent || terrainOptions->isDrapeLinesEnabled() || isLineSourceDensityForced());
                }
            }
        }
        if (!tileTransformer) {
            tileTransformer = std::make_shared<vt::DefaultTileTransformer>(static_cast<float>(Const::WORLD_SIZE));
        }
        _tileRenderer->setTileTransformer(tileTransformer);
    }

    TileLayer::FetchTaskBase::FetchTaskBase(const std::shared_ptr<TileLayer>& layer, long long tileId, const MapTile& tile, bool preloadingTile) :
        _layer(layer),
        _tileId(tileId),
        _tile(tile),
        _preloadingTile(preloadingTile),
        _dataSourceTiles(),
        _started(false),
        _invalidated(false)
    {
        for (MapTile dataSourceTile = tile; true; ) {
            int zoom = dataSourceTile.getZoom();
            if (zoom >= layer->getDataSource()->getMinZoom() && zoom <= layer->getDataSource()->getMaxZoom()) {
                _dataSourceTiles.push_back(dataSourceTile);
            }
            if (zoom <= 0) {
                break;
            }
            dataSourceTile = dataSourceTile.getParent();
        }
    }

    long long TileLayer::FetchTaskBase::getTileId() const {
        return _tileId;
    }
    
    MapTile TileLayer::FetchTaskBase::getMapTile() const {
        return _tile;
    }
    
    bool TileLayer::FetchTaskBase::isPreloadingTile() const {
        return _preloadingTile;
    }
    
    bool TileLayer::FetchTaskBase::isInvalidated() const {
        return _invalidated.load();
    }

    void TileLayer::FetchTaskBase::invalidate() {
        _invalidated.store(true);
    }
        
    void TileLayer::FetchTaskBase::cancel() {
        std::shared_ptr<TileLayer> layer = _layer.lock();
        if (!layer) {
            Log::Info("TileLayer::FetchTaskBase: Lost connection to layer");
            return;
        }

        bool cancel = false;
        {
            std::lock_guard<std::recursive_mutex> lock(layer->_mutex);
            if (!_started) {
                cancel = true;
            }
            CancelableTask::cancel();
        }

        if (cancel) {
            layer->_fetchingTileTasks.remove(_tileId, std::static_pointer_cast<FetchTaskBase>(shared_from_this()));
        }
    }

    void TileLayer::FetchTaskBase::run() {
        std::shared_ptr<TileLayer> layer = _layer.lock();
        if (!layer) {
            Log::Info("TileLayer::FetchTaskBase: Lost connection to layer");
            return;
        }
            
        {
            std::lock_guard<std::recursive_mutex> lock(layer->_mutex);
            if (isCanceled()) {
                return;
            }
            _started = true;
        }

        bool refresh = false;
        try {
            // Warm up the elevation grid cache so that tile geometry can be built
            // with the correct heights on the first try (elevation lookups during
            // decoding and surface building are non-blocking).
            if (auto options = layer->getOptions()) {
                if (auto terrainOptions = options->getTerrainOptions()) {
                    if (terrainOptions->isEnabled() && _tile.getZoom() >= terrainOptions->getMinZoom() && !isCanceled()) {
                        terrainOptions->getElevationManager()->getTileGrid(_tile, ElevationManager::LoadMode::ALLOW_LOAD);
                    }
                }
            }

            refresh = loadTile(layer) && !_preloadingTile;
            if (refresh) {
                loadUTFGridTile(layer);
            }
        }
        catch (const std::exception& ex) {
            Log::Errorf("TileLayer::FetchTaskBase: Exception while loading tile: %s", ex.what());
        }
    
        layer->_fetchingTileTasks.remove(_tileId, std::static_pointer_cast<FetchTaskBase>(shared_from_this()));

        if (refresh) {
            if (auto mapRenderer = layer->getMapRenderer()) {
                mapRenderer->layerChanged(layer->shared_from_this(), false);
                mapRenderer->requestRedraw();
            }
        }
    }
    
    bool TileLayer::FetchTaskBase::loadUTFGridTile(const std::shared_ptr<TileLayer>& tileLayer) {
        DirectorPtr<TileDataSource> dataSource = tileLayer->_utfGridDataSource;

        std::vector<MapTile> dataSourceTiles;
        if (dataSource) {
            for (MapTile dataSourceTile = _tile; true; ) {
                int zoom = dataSourceTile.getZoom();
                if (zoom >= dataSource->getMinZoom() && zoom <= dataSource->getMaxZoom()) {
                    dataSourceTiles.push_back(dataSourceTile);
                }
                if (zoom <= 0) {
                    break;
                }
                dataSourceTile = dataSourceTile.getParent();
            }
        }

        bool refresh = false;
        for (const MapTile& dataSourceTile : dataSourceTiles) {
            std::shared_ptr<TileData> tileData = dataSource->loadTile(dataSourceTile);
            if (!tileData) {
                break;
            }
            if (tileData->isReplaceWithParent()) {
                continue;
            }
            if (!tileData->getData()) {
                break;
            }

            std::shared_ptr<UTFGridTile> utfTile = UTFGridTile::DecodeUTFTile(tileData->getData());
            if (utfTile) {
                std::lock_guard<std::recursive_mutex> lock(tileLayer->_mutex);
                tileLayer->_utfGridTiles[dataSourceTile] = utfTile; // we ignore expiration info here
                refresh = true;
            } else {
                Log::Error("TileLayer::FetchTaskBase: Failed to decode UTF grid tile");
            }
            break;
        }
        return refresh;
    }

    const float TileLayer::DISCRETE_ZOOM_LEVEL_BIAS = 0.001f;

    // Measured at the demo's mountain camera: a sane cover is ~50 tiles and the pathological
    // one was 550, so this only engages on a configuration that is already unaffordable.
    const int TileLayer::TERRAIN_COVER_TILE_BUDGET = 256;

    const int TileLayer::MAX_PARENT_SEARCH_DEPTH = 6;
    // As deep as the parent search: a stand-in must be able to cover a zoom-in of several levels,
    // or the map goes EMPTY exactly when the user asked to see more. A deep stand-in only looked bad
    // while coarse parents were also being fetched AHEAD of the wanted tiles (PARENT_PRIORITY_OFFSET
    // was positive); with that order fixed, what is shown meanwhile is whatever was already there.
    const int TileLayer::MAX_STAND_IN_DEPTH = MAX_PARENT_SEARCH_DEPTH;
    const int TileLayer::MAX_CHILD_SEARCH_DEPTH = 3;

    // NEGATIVE on purpose: the parent fetched as a preview is dispatched AFTER the tiles that are
    // actually wanted. It used to be +1, so a coarse stand-in was requested first and the map showed
    // it even when the real tile would have arrived just as fast - and for a source that generates
    // its tiles (traced contours) that preview is a full pass whose result is thrown away.
    const int TileLayer::PARENT_PRIORITY_OFFSET = -1;
    const int TileLayer::PRELOADING_PRIORITY_OFFSET = -2;
    const double TileLayer::PRELOADING_TILE_SCALE = 1.5;
    
}

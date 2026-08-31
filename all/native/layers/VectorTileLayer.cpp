#include "VectorTileLayer.h"
#include "core/BinaryData.h"
#include "components/Exceptions.h"
#include "components/CancelableThreadPool.h"
#include "components/Options.h"
#include "geometry/VectorTileFeature.h"
#include "graphics/utils/BackgroundBitmapGenerator.h"
#include "graphics/utils/SkyBitmapGenerator.h"
#include "datasources/TileDataSource.h"
#include "layers/VectorTileEventListener.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "renderers/MapRenderer.h"
#include "renderers/TileRenderer.h"
#include "renderers/components/RayIntersectedElement.h"
#include "renderers/drawdatas/TileDrawData.h"
#include "ui/VectorTileClickInfo.h"
#include "utils/Log.h"
#include "utils/Const.h"
#include "vectortiles/VectorTileDecoder.h"
#include "vectortiles/MBVectorTileDecoder.h"

#include <vt/TileId.h>
#include <vt/Tile.h>
#include <vt/TileBackground.h>
#include <vt/TileLayer.h>
#include <vt/TileLayerBuilder.h>
#include <vt/TileTransformer.h>
#include <mapnikvt/ValueConverter.h>

namespace {

    template <typename T>
    std::optional<T> readDecoderParameter(const std::shared_ptr<massif::VectorTileDecoder>& decoder, const std::string& paramName) {
        if (auto symbolizerContextSettings = decoder->getSymbolizerContextSettings()) {
            if (auto parameterValueMap = symbolizerContextSettings->getStyleParameterValueMap()) { // snapshot
                auto it = parameterValueMap->find(paramName);
                if (it != parameterValueMap->end()) {
                    return std::optional<T>(massif::mvt::ValueConverter<T>::convert(it->second));
                }
            }
        }
        return std::optional<T>();
    }

}

namespace massif {

    VectorTileLayer::VectorTileLayer(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<VectorTileDecoder>& decoder) :
        TileLayer(dataSource),
        _vectorTileEventListener(),
        _labelRenderOrder(VectorTileRenderOrder::VECTOR_TILE_RENDER_ORDER_LAYER),
        _buildingRenderOrder(VectorTileRenderOrder::VECTOR_TILE_RENDER_ORDER_LAST),
        _clickRadius(4.0f),
        _layerBlendingSpeed(1.0f),
        _labelBlendingSpeed(1.0f),
        _rendererLayerFilter(),
        _clickHandlerLayerFilter(),
        _tileMapsMode(false),
        _tileDecoder(decoder),
        _tileDecoderListener(),
        _backgroundColor(0, 0, 0, 0),
        _backgroundBitmap(),
        _skyColor(0, 0, 0, 0),
        _skyGroundColor(0, 0, 0, 0),
        _skyBitmap(),
        _poleTiles(),
        _visibleTileIds(),
        _tempDrawDatas(),
        _visibleCache(DEFAULT_VISIBLE_CACHE_SIZE),
        _preloadingCache(DEFAULT_PRELOADING_CACHE_SIZE)
    {
        if (!decoder) {
            throw NullArgumentException("Null decoder");
        }

        setCullDelay(DEFAULT_CULL_DELAY);

        // A source that declares its format is authoritative, so take it over the per-tile
        // detection. 'encoding' is read first because MapLibre's own tilesets keep format at 'pbf'
        // and put the MLT-ness there. Only when the decoder is still on AUTO: an explicit
        // setTileFormat is the app's decision and stands.
        if (auto mbDecoder = std::dynamic_pointer_cast<MBVectorTileDecoder>(decoder)) {
            if (mbDecoder->getTileFormat() == TileFormat::TILE_FORMAT_AUTO && dataSource) {
                // getMetaDataElement, not getContainerMetaData: an app can declare the format on
                // the source itself for a container that does not carry one.
                auto readDeclaration = [&dataSource](const std::string& key) {
                    Variant value = dataSource->getMetaDataElement(key);
                    return value.getType() == VariantType::VARIANT_TYPE_STRING ? value.getString() : std::string();
                };
                TileFormat::TileFormat format = MBVectorTileDecoder::parseTileFormat(readDeclaration("encoding"));
                if (format == TileFormat::TILE_FORMAT_AUTO) {
                    format = MBVectorTileDecoder::parseTileFormat(readDeclaration("format"));
                }
                if (format != TileFormat::TILE_FORMAT_AUTO) {
                    mbDecoder->setTileFormat(format);
                }
            }
        }

        if (auto clickRadius = readDecoderParameter<float>(decoder, "_clickradius")) {
            setClickRadius(*clickRadius);
        }
        if (auto layerBlendingSpeed = readDecoderParameter<float>(decoder, "_layerblendingspeed")) {
            setLayerBlendingSpeed(*layerBlendingSpeed);
        }
        if (auto labelBlendingSpeed = readDecoderParameter<float>(decoder, "_labelblendingspeed")) {
            setLabelBlendingSpeed(*labelBlendingSpeed);
        }
        if (auto rendererLayerFilter = readDecoderParameter<std::string>(decoder, "_rendererlayerfilter")) {
            setRendererLayerFilter(*rendererLayerFilter);
        }
        if (auto clickHandlerLayerFilter = readDecoderParameter<std::string>(decoder, "_clickhandlerlayerfilter")) {
            setClickHandlerLayerFilter(*clickHandlerLayerFilter);
        }
    }
    
    VectorTileLayer::~VectorTileLayer() {
    }
    
    std::shared_ptr<VectorTileDecoder> VectorTileLayer::getTileDecoder() const {
        return _tileDecoder;
    }
    
    std::size_t VectorTileLayer::getTileCacheCapacity() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _preloadingCache.capacity();
    }
    
    void VectorTileLayer::setTileCacheCapacity(std::size_t capacityInBytes) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _preloadingCache.resize(capacityInBytes);
    }
    
    VectorTileRenderOrder::VectorTileRenderOrder VectorTileLayer::getLabelRenderOrder() const {
        return _labelRenderOrder.load();
    }
    
    void VectorTileLayer::setLabelRenderOrder(VectorTileRenderOrder::VectorTileRenderOrder renderOrder) {
        _labelRenderOrder.store(renderOrder);
        redraw();
    }
    
    VectorTileRenderOrder::VectorTileRenderOrder VectorTileLayer::getBuildingRenderOrder() const {
        return _buildingRenderOrder.load();
    }
    
    void VectorTileLayer::setBuildingRenderOrder(VectorTileRenderOrder::VectorTileRenderOrder renderOrder) {
        _buildingRenderOrder.store(renderOrder);
        redraw();
    }

    float VectorTileLayer::getClickRadius() const {
        return _clickRadius.load();
    }

    void VectorTileLayer::setClickRadius(float radius) {
        _clickRadius.store(radius);
    }

    float VectorTileLayer::getLayerBlendingSpeed() const {
        return _layerBlendingSpeed.load();
    }

    void VectorTileLayer::setLayerBlendingSpeed(float speed) {
        _layerBlendingSpeed.store(speed);
    }
    
    float VectorTileLayer::getLabelBlendingSpeed() const {
        return _labelBlendingSpeed.load();
    }
    
    void VectorTileLayer::setLabelBlendingSpeed(float speed) {
        _labelBlendingSpeed.store(speed);
    }

    std::string VectorTileLayer::getRendererLayerFilter() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _rendererLayerFilter;
    }

    void VectorTileLayer::setRendererLayerFilter(const std::string& filter) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::optional<std::regex> filterRe;
        try {
            if (!filter.empty()) {
                filterRe = std::regex(filter);
            }
            _rendererLayerFilter = filter;
        }
        catch (const std::regex_error& ex) {
            Log::Errorf("VectorTileLayer::setRendererLayerFilter: Invalid filter: %s", ex.what());
            throw InvalidArgumentException("Invalid filter expression");
        }
        _tileRenderer->setRendererLayerFilter(filterRe);
        updateTiles(false); // need to reload tiles to display the changes
    }

    std::string VectorTileLayer::getClickHandlerLayerFilter() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _clickHandlerLayerFilter;
    }

    void VectorTileLayer::setClickHandlerLayerFilter(const std::string& filter) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::optional<std::regex> filterRe;
        try {
            if (!filter.empty()) {
                filterRe = std::regex(filter);
            }
            _clickHandlerLayerFilter = filter;
        }
        catch (const std::regex_error& ex) {
            Log::Errorf("VectorTileLayer::setClickHandlerLayerFilter: Invalid filter: %s", ex.what());
            throw InvalidArgumentException("Invalid filter expression");
        }
        _tileRenderer->setClickHandlerLayerFilter(filterRe);
    }
    
    std::shared_ptr<VectorTileEventListener> VectorTileLayer::getVectorTileEventListener() const {
        return _vectorTileEventListener.get();
    }
    
    void VectorTileLayer::setVectorTileEventListener(const std::shared_ptr<VectorTileEventListener>& eventListener) {
        std::shared_ptr<VectorTileEventListener> oldEventListener = _vectorTileEventListener.get();
        _vectorTileEventListener.set(eventListener);
        _tileRenderer->setInteractionMode(eventListener.get() ? true : false);
        if (eventListener && !oldEventListener) {
            updateTiles(false); // we must reload the tiles, we do not keep full element information if this is not required
        }
    }
    
    long long VectorTileLayer::getTileId(const MapTile& mapTile) const {
        if (isTileMapsMode()) {
            return MapTile(mapTile.getX(), mapTile.getY(), mapTile.getZoom(), 0).getTileId();
        } else {
            return mapTile.getTileId();
        }
    }

    bool VectorTileLayer::tileExists(long long tileId, bool preloadingCache) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (preloadingCache) {
            return _preloadingCache.exists(tileId);
        } else {
            return _visibleCache.exists(tileId);
        }
    }
    
    bool VectorTileLayer::tileValid(long long tileId, bool preloadingCache) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (preloadingCache) {
            return _preloadingCache.exists(tileId) && _preloadingCache.valid(tileId);
        } else {
            return _visibleCache.exists(tileId) && _visibleCache.valid(tileId);
        }
    }

    bool VectorTileLayer::prefetchTile(long long tileId, bool preloadingTile) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_preloadingCache.exists(tileId) && _preloadingCache.valid(tileId)) {
            if (!preloadingTile) {
                _preloadingCache.move(tileId, _visibleCache); // move to visible cache, just in case the element gets trashed
            } else {
                _preloadingCache.get(tileId);
            }
            return true;
        }
        if (_visibleCache.exists(tileId) && _visibleCache.valid(tileId)) {
            _visibleCache.get(tileId); // do not move to preloading, it will be moved at later stage
            return true;
        }
        return false;
    }
    
    void VectorTileLayer::fetchTile(long long tileId, const MapTile& tile, bool preloadingTile, int priorityDelta) {
        std::shared_ptr<CancelableThreadPool> tileThreadPool;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            tileThreadPool = _tileThreadPool;
        }
        if (tileThreadPool) {
            auto task = std::make_shared<FetchTask>(std::static_pointer_cast<VectorTileLayer>(shared_from_this()), tileId, MapTile(tile.getX(), tile.getY(), tile.getZoom(), 0), preloadingTile);
            _fetchingTileTasks.insert(tileId, task);
            tileThreadPool->execute(task, getUpdatePriority() + priorityDelta);
        }
    }

    void VectorTileLayer::clearTiles(bool preloadingTiles) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (preloadingTiles) {
            _preloadingCache.clear();
        } else {
            _visibleCache.clear();
        }
    }

    void VectorTileLayer::invalidateTiles(bool preloadingTiles) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (preloadingTiles) {
            _preloadingCache.invalidate_all(std::chrono::steady_clock::now());
        } else {
            _visibleCache.invalidate_all(std::chrono::steady_clock::now());
        }
    }

    std::shared_ptr<VectorTileDecoder::TileMap> VectorTileLayer::getTileMap(long long tileId) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        TileInfo tileInfo;
        if (_visibleCache.peek(tileId, tileInfo)) {
            return tileInfo.getTileMap();
        }
        if (_preloadingCache.peek(tileId, tileInfo)) {
            return tileInfo.getTileMap();
        }
        return std::shared_ptr<VectorTileDecoder::TileMap>();
    }

    std::shared_ptr<vt::Tile> VectorTileLayer::getPoleTile(int y) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        mvt::ColorFunctionProperty poleColor = (y < 0 ? _tileDecoder->getMapSettings()->northPoleColor : _tileDecoder->getMapSettings()->southPoleColor);
        vt::ColorFunction colorFunc = poleColor.getFunction(getExpressionContext());
        std::shared_ptr<vt::Tile>& tile = _poleTiles[y < 0 ? 0 : 1];
        if (!tile || tile->getLayers().at(0)->getBackgrounds().at(0)->getColorFunc() != colorFunc) {
            auto tileBackground = std::make_shared<vt::TileBackground>(colorFunc, std::shared_ptr<const vt::BitmapPattern>());

            float tileSize = 256.0f; // 'normalized' tile size in pixels. Not really important
            vt::TileId vtTile(0, 0, y);
            vt::TileLayerBuilder tileLayerBuilder(std::string(), 0, vtTile, getTileTransformer(), tileSize, 1.0f); // Note: the size/scale argument is ignored
            tileLayerBuilder.addBackground(tileBackground);
            std::shared_ptr<vt::TileLayer> tileLayer = tileLayerBuilder.buildTileLayer();
            tile = std::make_shared<vt::Tile>(vtTile, tileSize, std::vector<std::shared_ptr<vt::TileLayer> > { tileLayer });
        }
        return tile;
    }
    
    void VectorTileLayer::collectLabelLayers(std::vector<std::shared_ptr<VectorTileLayer> >& labelLayers) {
        labelLayers.push_back(std::static_pointer_cast<VectorTileLayer>(shared_from_this()));
    }

    void VectorTileLayer::calculateDrawData(const MapTile& visTile, const MapTile& closestTile, bool preloadingTile) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        long long closestTileId = getTileId(closestTile);
        TileInfo tileInfo;
        _visibleCache.read(closestTileId, tileInfo);
        if (!tileInfo.getTileMap()) {
            _preloadingCache.read(closestTileId, tileInfo);
        }
        if (std::shared_ptr<VectorTileDecoder::TileMap> tileMap = tileInfo.getTileMap()) {
            auto it = tileMap->find(isTileMapsMode() ? closestTile.getFrameNr() : 0);
            if (it != tileMap->end()) {
                std::shared_ptr<const vt::Tile> tile = it->second;
                vt::TileId vtTileId(visTile.getZoom(), visTile.getX(), visTile.getY());
                if (closestTile.getZoom() > visTile.getZoom()) {
                    int dx = visTile.getX() >> visTile.getZoom();
                    int dy = visTile.getY() >> visTile.getZoom();
                    vtTileId = vt::TileId(closestTile.getZoom(), closestTile.getX() + (dx << closestTile.getZoom()), closestTile.getY() + (dy << closestTile.getZoom()));
                }
                _tempDrawDatas.push_back(std::make_shared<TileDrawData>(vtTileId, tile, closestTileId, preloadingTile));
            }
        }
    }
    
    void VectorTileLayer::refreshDrawData(const std::shared_ptr<CullState>& cullState, bool tilesChanged) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        // Get all tiles currently in the visible cache
        std::unordered_set<long long> lastVisibleCacheTiles = _visibleCache.keys();
        
        // Remember unused tiles from the visible cache
        for (const std::shared_ptr<TileDrawData>& drawData : _tempDrawDatas) {
            if (!drawData->isPreloadingTile()) {
                long long tileId = drawData->getTileId();
                lastVisibleCacheTiles.erase(tileId);

                if (!_visibleCache.exists(tileId) && _preloadingCache.exists(tileId)) {
                    _preloadingCache.move(tileId, _visibleCache);
                }
            }
        }
        
        // Move all unused tiles from visible cache to preloading cache
        for (long long tileId : lastVisibleCacheTiles) {
            _visibleCache.move(tileId, _preloadingCache);
        }
        
        // Update renderer if needed, run culler
        if (!(isSynchronizedRefresh() && _fetchingTileTasks.getVisibleCount() > 0)) {
            std::vector<std::shared_ptr<TileDrawData>> drawDatas = _tempDrawDatas;

            // Add poles
            if (auto options = getOptions()) {
                if (options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_SPHERICAL) {
                    const cglib::frustum3<double>& frustum = cullState->getViewState().getFrustum();
                    for (int y = -1; y <= 1; y += 2) {
                        vt::TileId vtTile(0, 0, y);
                        cglib::bbox3<double> bbox = getTileTransformer()->calculateTileBBox(vtTile);
                        if (frustum.inside(bbox)) {
                            drawDatas.push_back(std::make_shared<TileDrawData>(vtTile, getPoleTile(y), -1, false));
                        }
                    }
                }
            }
            
            if (_tileRenderer->refreshTiles(drawDatas)) {
                tilesChanged = true;
            }
        }
    
        if (auto mapRenderer = getMapRenderer()) {
            if (tilesChanged) {
                mapRenderer->vtLabelsChanged(shared_from_this(), false);
                mapRenderer->requestRedraw();
            }
        }

        // Update visible tile ids, clear temporary draw data list
        _visibleTileIds.clear();
        for (const std::shared_ptr<TileDrawData>& drawData : _tempDrawDatas) {
            _visibleTileIds.push_back(drawData->getTileId());
        }
        _tempDrawDatas.clear();
    }
    
    int VectorTileLayer::getMinZoom() const {
        return std::max(_dataSource->getMinZoom(), _tileDecoder->getMinZoom());
    }
    
    int VectorTileLayer::getMaxZoom() const {
        return _tileDecoder->getMaxZoom(); // NOTE: datasource max zoom is handled differently
    }    
    
    std::vector<long long> VectorTileLayer::getVisibleTileIds() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _visibleTileIds;
    }

    void VectorTileLayer::calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const {
        DirectorPtr<VectorTileEventListener> eventListener = _vectorTileEventListener;

        if (eventListener) {
            float clickRadius = getClickRadius();

            for (int pass = 0; pass < 2; pass++) {
                std::vector<vt::GLTileRenderer::GeometryIntersectionInfo> hitResults;
                if (pass == 0) {
                    _tileRenderer->calculateRayIntersectedElements(ray, viewState, clickRadius, hitResults);
                } else {
                    _tileRenderer->calculateRayIntersectedElements3D(ray, viewState, clickRadius, hitResults);
                }

                for (const vt::GLTileRenderer::GeometryIntersectionInfo& hitResult : hitResults) {
                    std::lock_guard<std::recursive_mutex> lock(_mutex);

                    long long tileId = getTileId(MapTile(hitResult.tileId.x, hitResult.tileId.y, hitResult.tileId.zoom, getFrameNr()));
                    TileInfo tileInfo;
                    _visibleCache.peek(tileId, tileInfo);
                    if (!tileInfo.getTileMap()) {
                        _preloadingCache.peek(tileId, tileInfo);
                    }
                    if (std::shared_ptr<BinaryData> data = tileInfo.getTileData()) {
                        if (std::shared_ptr<VectorTileFeature> tileFeature = _tileDecoder->decodeFeature(hitResult.featureId, hitResult.tileId, data, tileInfo.getTileBounds())) {
                            std::shared_ptr<Layer> thisLayer = std::const_pointer_cast<Layer>(shared_from_this());
                            results.push_back(RayIntersectedElement(tileFeature, thisLayer, ray(hitResult.rayT), ray(hitResult.rayT), pass > 0, hitResult.geoPointIndex));
                        } else {
                            Log::Warnf("VectorTileLayer::calculateRayIntersectedElements: Failed to decode feature %lld", hitResult.featureId);
                        }
                    } else {
                        Log::Warn("VectorTileLayer::calculateRayIntersectedElements: Failed to find tile data");
                    }
                }
            }
        }

        TileLayer::calculateRayIntersectedElements(ray, viewState, results);
    }

    bool VectorTileLayer::processClick(const ClickInfo& clickInfo, const RayIntersectedElement& intersectedElement, const ViewState& viewState) const {
        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface) {
            return false;
        }
        
        DirectorPtr<VectorTileEventListener> eventListener = _vectorTileEventListener;

        if (eventListener) {
            if (auto tileFeature = intersectedElement.getElement<VectorTileFeature>()) {
                MapPos hitPos = _dataSource->getProjection()->fromInternal(projectionSurface->calculateMapPos(intersectedElement.getHitPos()));
                auto vectorClickInfo = std::make_shared<VectorTileClickInfo>(clickInfo, hitPos, hitPos, tileFeature, intersectedElement.getLayer(), intersectedElement.getPointIndex());
                return eventListener->onVectorTileClicked(vectorClickInfo);
            }
        }

        return TileLayer::processClick(clickInfo, intersectedElement, viewState);
    }

    void VectorTileLayer::setComponents(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool,
                                        const std::shared_ptr<CancelableThreadPool>& tileThreadPool,
                                        const std::weak_ptr<Options>& options,
                                        const std::weak_ptr<MapRenderer>& mapRenderer,
                                        const std::weak_ptr<TouchHandler>& touchHandler)
    {
        TileLayer::setComponents(envelopeThreadPool, tileThreadPool, options, mapRenderer, touchHandler);

        // The decoder rasterizes the glyphs of a label, and it can only pick a raster size that
        // suits the label if it knows what a style pixel is worth on this display.
        if (std::shared_ptr<Options> opts = options.lock()) {
            _tileDecoder->setPixelScale(static_cast<float>(opts->getDPI() / Const::UNSCALED_DPI));
        }
    }

    void VectorTileLayer::offsetLayerHorizontally(double offset) {
        _tileRenderer->offsetLayerHorizontally(offset);
    }
    
    bool VectorTileLayer::onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState) {
        {
            // The style's own sun/shadow/fog values for this frame. Read here rather than inside
            // the renderer because only the layer can evaluate its style's expressions.
            StyleEnvironment env;
            getStyleEnvironment(viewState, env);
            _tileRenderer->setStyleEnvironment(env);
        }
        updateTileLoadListener();

        if (auto mapRenderer = getMapRenderer()) {
            float opacity = getOpacity();

            if (opacity < 1.0f) {
                mapRenderer->clearAndBindScreenFBO(Color(0, 0, 0, 0), true, true);
            }

            _tileRenderer->setLabelOrder(static_cast<int>(getLabelRenderOrder()));
            _tileRenderer->setBuildingOrder(static_cast<int>(getBuildingRenderOrder()));
            _tileRenderer->setLayerBlendingSpeed(getLayerBlendingSpeed());
            _tileRenderer->setLabelBlendingSpeed(getLabelBlendingSpeed());
            bool refresh = _tileRenderer->onDrawFrame(deltaSeconds, viewState);

            if (opacity < 1.0f) {
                mapRenderer->blendAndUnbindScreenFBO(opacity);
            }

            return refresh;
        }
        return false;
    }
        
    bool VectorTileLayer::onDrawFrame3D(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState) {
        if (auto mapRenderer = getMapRenderer()) {
            return _tileRenderer->onDrawFrame3D(deltaSeconds, viewState);
        }
        return false;
    }
    
    std::shared_ptr<Bitmap> VectorTileLayer::getBackgroundBitmap(const ViewState& viewState) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        Color backgroundColor = TileRenderer::evaluateColorFunc(_tileDecoder->getMapSettings()->backgroundColor.getFunction(getExpressionContext()), viewState);
        if (backgroundColor != _backgroundColor || !_backgroundBitmap) {
            if (backgroundColor != Color(0, 0, 0, 0)) {
                _backgroundBitmap = BackgroundBitmapGenerator(BACKGROUND_BLOCK_SIZE, BACKGROUND_BLOCK_COUNT).generateBitmap(backgroundColor);
            } else {
                _backgroundBitmap.reset();
            }
            _backgroundColor = backgroundColor;
        }
        return _backgroundBitmap;
    }

    Color VectorTileLayer::getBackgroundColor(const ViewState& viewState) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        std::shared_ptr<const mvt::Map::Settings> mapSettings = _tileDecoder->getMapSettings();
        Color color = TileRenderer::evaluateColorFunc(mapSettings->backgroundColor.getFunction(getExpressionContext()), viewState);
        // The background is a Map setting, so it misses the grade every symbolizer colour gets at
        // draw time - and it is the largest surface on the map. Lit here by the same rule: at an
        // emissive of 1, which is the default and what a pre-lit style leaves it at, this is a
        // no-op.
        float emissive = TileRenderer::evaluateFloatFunc(mapSettings->backgroundEmissive.getFunction(getExpressionContext()), viewState);
        if (emissive < 1.0f) {
            if (std::shared_ptr<Options> options = getOptions()) {
                StyleEnvironment env;
                getStyleEnvironment(viewState, env);
                ResolvedLighting lighting = resolveLighting(options->getLightOptions(), env);
                auto lit = [&](unsigned char c, int i) {
                    float value = c / 255.0f * (emissive + (1.0f - emissive) * lighting.radiance(i));
                    return static_cast<unsigned char>(std::max(0.0f, std::min(1.0f, value)) * 255.0f + 0.5f);
                };
                color = Color(lit(color.getR(), 0), lit(color.getG(), 1), lit(color.getB(), 2), color.getA());
            }
        }
        return color;
    }

    bool VectorTileLayer::getStyleEnvironment(const ViewState& viewState, StyleEnvironment& env) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        std::shared_ptr<const mvt::Map::Settings> mapSettings = _tileDecoder->getMapSettings();
        if (!mapSettings) {
            return false;
        }
        mvt::ExpressionContext context = getExpressionContext();
        // Only what the style actually declares: isDefined() is false for a property the style
        // never mentions, and that one keeps coming from the application's own options.
        auto readFloat = [&](const mvt::FloatFunctionProperty& property, std::optional<float>& value) {
            if (property.isDefined()) {
                value = TileRenderer::evaluateFloatFunc(property.getFunction(context), viewState);
            }
        };
        // A flag written as a number, like building-rounded-roof beside it.
        auto readBool = [&](const mvt::FloatFunctionProperty& property, std::optional<bool>& value) {
            if (property.isDefined()) {
                value = TileRenderer::evaluateFloatFunc(property.getFunction(context), viewState) != 0.0f;
            }
        };
        auto readColor = [&](const mvt::ColorFunctionProperty& property, std::optional<Color>& value) {
            if (property.isDefined()) {
                value = TileRenderer::evaluateColorFunc(property.getFunction(context), viewState);
            }
        };
        readFloat(mapSettings->sunAzimuth, env.sunAzimuth);
        readFloat(mapSettings->sunAltitude, env.sunAltitude);
        readColor(mapSettings->sunColor, env.sunColor);
        readFloat(mapSettings->sunIntensity, env.sunIntensity);
        readFloat(mapSettings->ambientIntensity, env.ambientIntensity);
        readColor(mapSettings->ambientColor, env.ambientColor);
        readFloat(mapSettings->buildingLightIntensity, env.buildingLightIntensity);
        readFloat(mapSettings->buildingAmbient, env.buildingAmbient);
        readFloat(mapSettings->buildingVerticalGradient, env.buildingVerticalGradient);
        readFloat(mapSettings->buildingRoofShade, env.buildingRoofShade);
        readBool(mapSettings->colorsPrelit, env.colorsPrelit);
        readFloat(mapSettings->buildingEmissive, env.buildingEmissive);
        readFloat(mapSettings->backgroundEmissive, env.backgroundEmissive);
        readFloat(mapSettings->buildingHeightScale, env.buildingHeightScale);
        readFloat(mapSettings->buildingHeightViewScale, env.buildingHeightViewScale);
        readBool(mapSettings->buildingGrowOnAppear, env.buildingGrowOnAppear);
        readBool(mapSettings->buildingFadeOnAppear, env.buildingFadeOnAppear);
        readFloat(mapSettings->buildingAoIntensity, env.buildingAoIntensity);
        readFloat(mapSettings->textOcclusionOpacity, env.textOcclusionOpacity);
        readFloat(mapSettings->buildingAoGroundAttenuation, env.buildingAoGroundAttenuation);
        readFloat(mapSettings->shadowStrength, env.shadowStrength);
        readFloat(mapSettings->shadowBias, env.shadowBias);
        readFloat(mapSettings->shadowSoftness, env.shadowSoftness);
        readFloat(mapSettings->shadowDistance, env.shadowDistance);
        if (mapSettings->fogEnabled.isDefined()) {
            env.fogEnabled = TileRenderer::evaluateFloatFunc(mapSettings->fogEnabled.getFunction(context), viewState) != 0.0f;
        }
        readColor(mapSettings->fogColor, env.fogColor);
        readFloat(mapSettings->fogRangeStart, env.fogRangeStart);
        readFloat(mapSettings->fogRangeEnd, env.fogRangeEnd);
        readColor(mapSettings->fogHighColor, env.fogHighColor);
        readColor(mapSettings->fogSpaceColor, env.fogSpaceColor);
        readFloat(mapSettings->fogHorizonBlend, env.fogHorizonBlend);
        readFloat(mapSettings->fogStarIntensity, env.fogStarIntensity);
        readFloat(mapSettings->fogVerticalRangeStart, env.fogVerticalRangeStart);
        readFloat(mapSettings->fogVerticalRangeEnd, env.fogVerticalRangeEnd);
        readFloat(mapSettings->skyType, env.skyType);
        readFloat(mapSettings->skyAtmosphereSunIntensity, env.skyAtmosphereSunIntensity);
        readColor(mapSettings->skyAtmosphereColor, env.skyAtmosphereColor);
        readColor(mapSettings->skyAtmosphereHaloColor, env.skyAtmosphereHaloColor);
        readFloat(mapSettings->skyAtmosphereLuminance, env.skyAtmosphereLuminance);
        readFloat(mapSettings->terrainMaxVisibleDistance, env.terrainMaxVisibleDistance);
        if (mapSettings->terrainLighting.isDefined()) {
            env.terrainLightingEnabled = TileRenderer::evaluateFloatFunc(mapSettings->terrainLighting.getFunction(context), viewState) != 0.0f;
        }
        auto readInt = [&](const mvt::FloatFunctionProperty& property, std::optional<int>& value) {
            if (property.isDefined()) {
                value = static_cast<int>(TileRenderer::evaluateFloatFunc(property.getFunction(context), viewState) + 0.5f);
            }
        };
        readInt(mapSettings->shadowMapSize, env.shadowMapSize);
        readInt(mapSettings->shadowCascades, env.shadowCascades);
        readInt(mapSettings->shadowCasterMargin, env.shadowCasterMargin);
        return !env.empty();
    }

    std::shared_ptr<Bitmap> VectorTileLayer::getSkyBitmap(const ViewState& viewState) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        auto options = getOptions();
        if (!options) {
            return std::shared_ptr<Bitmap>();
        }

        Color skyGroundColor = TileRenderer::evaluateColorFunc(_tileDecoder->getMapSettings()->backgroundColor.getFunction(getExpressionContext()), viewState);
        Color skyColor = options->getSkyColor();
        if (skyGroundColor != _skyGroundColor || skyColor != _skyColor || !_skyBitmap) {
            if (skyColor == Color(0, 0, 0, 0)) {
                _skyBitmap.reset();
            } else {
                _skyBitmap = SkyBitmapGenerator(1, SKY_BITMAP_HEIGHT).generateBitmap(skyGroundColor, skyColor);
            }
            _skyGroundColor = skyGroundColor;
            _skyColor = skyColor;
        }
        return _skyBitmap;
    }

    void VectorTileLayer::registerDataSourceListener() {
        _tileDecoderListener = std::make_shared<TileDecoderListener>(std::static_pointer_cast<VectorTileLayer>(shared_from_this()));
        _tileDecoder->registerOnChangeListener(_tileDecoderListener);
    
        _dataSourceListener = std::make_shared<DataSourceListener>(std::static_pointer_cast<VectorTileLayer>(shared_from_this()));
        _dataSource->registerOnChangeListener(_dataSourceListener);
    }
    
    void VectorTileLayer::unregisterDataSourceListener() {
        _dataSource->unregisterOnChangeListener(_dataSourceListener);
        _dataSourceListener.reset();

        _tileDecoder->unregisterOnChangeListener(_tileDecoderListener);
        _tileDecoderListener.reset();
    }

    bool VectorTileLayer::isTileMapsMode() const {
        return _tileMapsMode.load();
    }

    void VectorTileLayer::setTileMapsMode(bool enabled) {
        _tileMapsMode.store(enabled);
    }

    mvt::ExpressionContext VectorTileLayer::getExpressionContext() const {
        mvt::ExpressionContext exprContext;
        if (auto symbolizerContextSettings = _tileDecoder->getSymbolizerContextSettings()) {
            exprContext.setStyleParameterStore(symbolizerContextSettings->getStyleParameterStore());
        }
        // Same source as the tiles use, so Map-block properties read the same render::3d
        if (std::shared_ptr<vt::TileTransformer> tileTransformer = getTileTransformer()) {
            exprContext.setRender3D(tileTransformer->isElevationBased());
        }
        return exprContext;
    }
    
    VectorTileLayer::TileDecoderListener::TileDecoderListener(const std::shared_ptr<VectorTileLayer>& layer) :
        _layer(layer)
    {
    }
        
    void VectorTileLayer::TileDecoderListener::onDecoderChanged() {
        if (std::shared_ptr<VectorTileLayer> layer = _layer.lock()) {
            layer->updateTiles(false);
        } else {
            Log::Error("VectorTileLayer::TileDecoderListener: Lost connection to layer");
        }
    }

    void VectorTileLayer::TileDecoderListener::onDecoderRefreshed() {
        // The decoded tiles already read the new value through the parameter store, and the colours
        // and widths that read it are evaluated per frame - so the next frame is the only thing that
        // has to change. No tile is fetched, decoded or re-culled.
        if (std::shared_ptr<VectorTileLayer> layer = _layer.lock()) {
            if (std::shared_ptr<MapRenderer> mapRenderer = layer->getMapRenderer()) {
                mapRenderer->requestRedraw();
            }
        } else {
            Log::Error("VectorTileLayer::TileDecoderListener: Lost connection to layer");
        }
    }
    
    VectorTileLayer::FetchTask::FetchTask(const std::shared_ptr<VectorTileLayer>& layer, long long tileId, const MapTile& tile, bool preloadingTile) :
        FetchTaskBase(layer, tileId, tile, preloadingTile)
    {
    }
    
    bool VectorTileLayer::FetchTask::loadTile(const std::shared_ptr<TileLayer>& tileLayer) {
        auto layer = std::static_pointer_cast<VectorTileLayer>(tileLayer);
        
        bool refresh = false;
        for (const MapTile& dataSourceTile : _dataSourceTiles) {
            if (isCanceled()) {
                break;
            }
            std::shared_ptr<TileData> tileData = layer->_dataSource->loadTile(dataSourceTile);
            if (!tileData) {
                break;
            }
            if (tileData->isReplaceWithParent()) {
                continue;
            }
            if(tileData->isOverZoom()) {
                // we need to invalidate cache tiles to make sure we dont draw over
                layer->_preloadingCache.remove(_tileId);
                layer->_visibleCache.remove(_tileId);
            }

            if (isCanceled()) {
                break;
            }

            // Decode vector tile.
            vt::TileId vtTile(_tile.getZoom(), _tile.getX(), _tile.getY());
            vt::TileId vtDataSourceTile(dataSourceTile.getZoom(), dataSourceTile.getX(), dataSourceTile.getY());
            std::shared_ptr<vt::TileTransformer> tileTransformer = layer->getTileTransformer();
            std::shared_ptr<VectorTileDecoder::TileMap> tileMap;
            if (std::shared_ptr<BinaryData> data = tileData->getData()) {
                tileMap = layer->_tileDecoder->decodeTile(vtDataSourceTile, vtTile, tileTransformer, data);
                if (!tileMap && !data->empty()) {
                    Log::Error("VectorTileLayer::FetchTask: Failed to decode tile");
                }
            }

            // Construct tile info - keep original data if interactivity is required
            VectorTileLayer::TileInfo tileInfo(layer->calculateMapTileBounds(dataSourceTile.getFlipped()), layer->_vectorTileEventListener.get() ? tileData->getData() : std::shared_ptr<BinaryData>(), tileMap);
            {
                std::lock_guard<std::recursive_mutex> lock(layer->_mutex);

                // Store the decoded tile in cache, unless invalidated.
                if (!isInvalidated()) {
                    if (layer->getTileTransformer() == tileTransformer) { // extra check that the tile is created with correct transformer. Otherwise simply drop it.
                        if (isPreloadingTile()) {
                            layer->_preloadingCache.put(_tileId, tileInfo, tileInfo.getSize());
                            if (tileData->getMaxAge() >= 0) {
                                layer->_preloadingCache.invalidate(_tileId, std::chrono::steady_clock::now() + std::chrono::milliseconds(tileData->getMaxAge()));
                            }
                        } else {
                            layer->_visibleCache.put(_tileId, tileInfo, tileInfo.getSize());
                            if (tileData->getMaxAge() >= 0) {
                                layer->_visibleCache.invalidate(_tileId, std::chrono::steady_clock::now() + std::chrono::milliseconds(tileData->getMaxAge()));
                            }
                        }
                    }
                }
            }
            
            // Debug tile performance issues
            if (Log::IsShowDebug()) {
                if (tileInfo.getMaxDrawCallCount() >= 20) {
                    Log::Debugf("VectorTileLayer::FetchTask: Tile requires %d draw calls", tileInfo.getMaxDrawCallCount());
                }
            }
                
            refresh = true; // NOTE: need to refresh even when invalidated
            break;
        }
        
        return refresh;
    }

    int VectorTileLayer::TileInfo::getMaxDrawCallCount() const {
        int maxDrawCallCount = 0;
        if (_tileMap) {
            for (auto it = _tileMap->begin(); it != _tileMap->end(); it++) {
                int drawCallCount = 0;
                for (const std::shared_ptr<vt::TileLayer>& layer : it->second->getLayers()) {
                    drawCallCount += static_cast<int>(layer->getBitmaps().size() + layer->getGeometries().size());
                }
                maxDrawCallCount = std::max(maxDrawCallCount, drawCallCount);
            }
        }
        return maxDrawCallCount;
    }

    std::size_t VectorTileLayer::TileInfo::getSize() const {
        std::size_t size = EXTRA_TILE_FOOTPRINT;
        if (_tileData) {
            size += _tileData->size();
        }
        if (_tileMap) {
            for (auto it = _tileMap->begin(); it != _tileMap->end(); it++) {
                size += it->second->getResidentSize();
            }
        }
        return size;
    }
    
    const int VectorTileLayer::BACKGROUND_BLOCK_SIZE = 16;
    const int VectorTileLayer::BACKGROUND_BLOCK_COUNT = 16;
    const int VectorTileLayer::SKY_BITMAP_HEIGHT = 128;

    const int VectorTileLayer::DEFAULT_CULL_DELAY = 200;

    const unsigned int VectorTileLayer::EXTRA_TILE_FOOTPRINT = 4096;
    const unsigned int VectorTileLayer::DEFAULT_VISIBLE_CACHE_SIZE = 512 * 1024 * 1024; // NOTE: the limit should never be reached in normal cases
    const unsigned int VectorTileLayer::DEFAULT_PRELOADING_CACHE_SIZE = 10 * 1024 * 1024;

}

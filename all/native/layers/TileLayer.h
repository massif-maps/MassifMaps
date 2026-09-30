/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILELAYER_H_
#define _MASSIF_TILELAYER_H_

#include "core/MapPos.h"
#include "core/MapBounds.h"
#include "core/MapTile.h"
#include "components/CancelableTask.h"
#include "components/DirectorPtr.h"
#include "datasources/TileDataSource.h"
#include "layers/Layer.h"
#include "layers/TerrainDecodeWait.h"

#include <vt/TileId.h>

#include <atomic>
#include <map>
#include <mutex>
#include <unordered_map>

namespace massif {
    class CancelableTask;
    class CullState;
    class ElevationManager;
    class GLResourceManager;
    class Projection;
class ProjectionSurface;
    class TerrainOptions;
    class TileRenderer;
    class TileLoadListener;
    class UTFGridTile;
    class UTFGridEventListener;
    namespace vt {
        class TileTransformer;
    }
    
    namespace TileSubstitutionPolicy {
        /**
         * The policy to use when looking for tiles that are not available.
         */
        enum TileSubstitutionPolicy {
            /**
             * Consider all cached/loaded tiles.
             */
            TILE_SUBSTITUTION_POLICY_ALL,
            /**
             * Consider only tiles that are currently visible.
             * This is recommended for low-latency data sources, like offline sources.
             */
            TILE_SUBSTITUTION_POLICY_VISIBLE,
            /**
             * Never substitute tiles.
             */
            TILE_SUBSTITUTION_POLICY_NONE
        };
    }
        
    /**
     * An abstract base class for all tile layers.
     */
    class TileLayer : public Layer {
    public:
        virtual ~TileLayer();
        
        /**
         * Returns the data source assigned to this layer.
         * @return The tile data source assigned to this layer.
         */
        std::shared_ptr<TileDataSource> getDataSource() const;

        /**
         * Returns the projection this layer's data is in, which is its data source's.
         * @return The projection, or null when the layer has no data source.
         */
        std::shared_ptr<Projection> getProjection() const;

        /**
         * Returns the tile data source of the associated UTF grid. By default this is null.
         * @return The tile data source of the associated UTF grid.
         */
        std::shared_ptr<TileDataSource> getUTFGridDataSource() const;
        /**
         * Sets the tile data source of the associated UTF grid.
         * @param dataSource The data source to use. Can be null if UTF grid is not used.
         */
        void setUTFGridDataSource(const std::shared_ptr<TileDataSource>& dataSource);
    
        /**
         * How many tiles the last cull put on screen. A diagnostic: it is what the tile LOD numbers actually cost.
         * @return The tile count after the last cull pass.
         */
        int getVisibleTileCount() const;
        /**
         * @return The count of tiles fetched around the visible ones, but not drawn.
         */
        int getPreloadingTileCount() const;

        /**
         * Returns the current frame number.
         * @return The current frame number.
         */
        int getFrameNr() const;
        /**
         * Sets the frame number, only used for animated tiles. 
         * Loading a new frame may take some time, previous frame is shown during loading.
         * @param frameNr The frame number to display.
         */
        void setFrameNr(int frameNr);
    
        /**
         * Returns the state of the preloading flag of this layer.
         * @return True if preloading is enabled.
         */
        bool isPreloading() const;
        /**
         * Sets the state of preloading for this layer: tiles adjacent to the visible ones are downloaded too, so panning shows no gaps.
         * It may cost some performance on slower devices and considerably increases network traffic with online maps.
         * The default is false.
         * @param preloading The new preloading state of the layer.
         */
        virtual void setPreloading(bool preloading);

        /**
         * Returns the state of the synchronized refresh flag.
         * @return The state of the synchronized refresh flag.
         */
        bool isSynchronizedRefresh() const;
        /**
         * Sets the state of the synchronized refresh flag. If disabled all tiles will appear on screen
         * one by one as they finish loading. If enabled the map will wait for all the visible tiles to finish loading
         * and then show them all on screen together. This is useful for animated tiles.
         * @param synchronizedRefresh The new state of the synchronized refresh flag.
         */
        void setSynchronizedRefresh(bool synchronizedRefresh);
    
        /**
         * Returns the current tile substitution policy.
         * @return The current substitution policy. Default is TILE_SUBSTITUTION_POLICY_ALL.
         */
        TileSubstitutionPolicy::TileSubstitutionPolicy getTileSubstitutionPolicy() const;
        /**
         * Sets the current tile substitution policy.
         * @param policy The new substitution policy. Default is TILE_SUBSTITUTION_POLICY_ALL.
         */
        void setTileSubstitutionPolicy(TileSubstitutionPolicy::TileSubstitutionPolicy policy);
        
        /**
         * Gets the current zoom level bias for this layer.
         * @return The current zoom level bias for this layer.
         */
        float getZoomLevelBias() const;
        /**
         * Sets the zoom level bias for this layer.
         * Higher zoom level bias forces SDK to use more detailed tiles for given view compared to lower zoom bias.
         * The default bias is 0.
         * @param bias The new bias value, both positive and negative fractional values are supported.
         */
        virtual void setZoomLevelBias(float bias);
        
        /**
         * Gets the current maximum overzoom level for this layer.
         * @return The current maximum overzoom level for this layer.
         */
        int getMaxOverzoomLevel() const;
        /**
         * Sets the maximum overzoom level for this layer.
         * If a tile for the given zoom level Z is not available, SDK will try to use tiles with zoom levels Z-1, ..., Z-MaxOverzoomLevel.
         * The default is 6.
         * @param overzoomLevel The new maximum overzoom value.
         */
        void setMaxOverzoomLevel(int overzoomLevel);

        /**
         * Gets how many zoom levels up a cached tile may stand in for a missing one.
         * @return The current maximum stand-in level for this layer.
         */
        int getMaxStandInLevel() const;
        /**
         * Sets how many zoom levels up a cached tile may stand in for a missing one while it loads (unlike MaxOverzoomLevel,
         * which bounds where the data of a missing tile comes from). The default is 6. Lower it (1 = immediate parent only)
         * for a source whose look changes so much with zoom, like generated contours, that a coarse stand-in is worse than nothing.
         * @param standInLevel The new maximum stand-in level.
         */
        void setMaxStandInLevel(int standInLevel);

        /**
         * Gets the current maximum underzoom level for this layer.
         * @return The current maximum underzoom level for this layer.
         */
        int getMaxUnderzoomLevel() const;

        /**
         * Sets the maximum underzoom level for this layer.
         * If a tile for the given zoom level Z is not available, SDK will try to use tiles with zoom levels Z-1, ..., Z-MaxOverzoomLevel and then Z+1, ..., Z+MaxUnderzoomLevel.
         * The default is 3.
         * @param underzoomLevel The new maximum underzoom value.
         */
        void setMaxUnderzoomLevel(int underzoomLevel);
        
        /**
         * Calculates the tile corresponding to given geographical coordinates and zoom level.
         * Note: zoom level bias is NOT applied, only discrete zoom level is used.
         * @param mapPos Coordinates of the point in data source projection coordinate system.
         * @param zoom Zoom level to use for the tile.
         * @return The corresponding map tile.
         */
        MapTile calculateMapTile(const MapPos& mapPos, int zoom) const;
        /**
         * Calculates the origin of given map tile.
         * @param mapTile The map tile to use.
         * @return The corresponding coordinates of the tile origin in data source projection coordinate system.
         */
        MapPos calculateMapTileOrigin(const MapTile& mapTile) const;
        /**
         * Calculates the bounds of given map tile.
         * @param mapTile The map tile to use.
         * @return The corresponding bounds of the tile origin in data source projection coordinate system.
         */
        MapBounds calculateMapTileBounds(const MapTile& mapTile) const;

        /**
         * Clears layer tile caches. This will release memory allocated to tiles.
         * @param all True if all tiles should be released, otherwise only preloading (invisible) tiles are released.
         */
        void clearTileCaches(bool all);

        /**
         * Returns the tile load listener.
         * @return The tile load listener.
         */
        std::shared_ptr<TileLoadListener> getTileLoadListener() const;
        /**
         * Sets the tile load listener.
         * @param tileLoadListener The tile load listener.
         */
        void setTileLoadListener(const std::shared_ptr<TileLoadListener>& tileLoadListener);

        /**
         * Returns the UTF grid event listener.
         * @return The UTF grid event listener.
         */
        std::shared_ptr<UTFGridEventListener> getUTFGridEventListener() const;
        /**
         * Sets the UTF grid event listener.
         * @param utfGridEventListener The UTF grid event listener.
         */
        void setUTFGridEventListener(const std::shared_ptr<UTFGridEventListener>& utfGridEventListener);
    
        virtual bool isUpdateInProgress() const;

        /**
         * Whether the visible tiles are all decoded for the terrain state the layer is now in. False
         * from the moment a 2D/3D switch invalidates them until their replacements have arrived; the
         * switch waits on it before it lets the terrain rise. Internal method.
         * @return True if no tile is still waiting for the current terrain decode state.
         */
        virtual bool isTerrainDecodeSettled();

        /** Tile passes completed so far. Internal method. */
        unsigned int getTileCalculationCount() const;
        /** A tile pass has completed since `count` and no visible tile is still loading. Internal method. */
        bool areVisibleTilesSettledSince(unsigned int count) const;

        /**
         * How many visible tiles the 2D/3D switch still waits on, -1 before the next cull. Internal method.
         * @return The number of pending tiles in this layer.
         */
        virtual int getTerrainDecodePendingCount() const;

    protected:
        /**
         * Marks the visible tiles as waiting for a new terrain decode; the next cull names which ones.
         */
        void markTerrainDecodeUnsettled();

        class DataSourceListener : public TileDataSource::OnChangeListener {
        public:
            explicit DataSourceListener(const std::shared_ptr<TileLayer>& layer);
            
            virtual void onTilesChanged(bool removeTiles);
            
        private:
            std::weak_ptr<TileLayer> _layer;
        };
        
        class FetchTaskBase : public CancelableTask {
        public:
            FetchTaskBase(const std::shared_ptr<TileLayer>& layer, long long tileId, const MapTile& tile, bool preloadingTile);
            
            long long getTileId() const;
            MapTile getMapTile() const;
            bool isPreloadingTile() const;

            bool isInvalidated() const;
            void invalidate();

            virtual void cancel();
            virtual void run();
            
        protected:
            virtual bool loadTile(const std::shared_ptr<TileLayer>& layer) = 0;
            
            std::weak_ptr<TileLayer> _layer;
            long long _tileId;
            MapTile _tile; // original tile
            bool _preloadingTile;
            std::vector<MapTile> _dataSourceTiles; // tiles in valid datasource range, ordered to top

        private:
            bool loadUTFGridTile(const std::shared_ptr<TileLayer>& layer);

            bool _started;
            std::atomic<bool> _invalidated;
        };
        
        class FetchingTileTasks {
        public:
            FetchingTileTasks() : _fetchingTiles(), _mutex() { }
            
            std::vector<std::shared_ptr<FetchTaskBase> > get(long long tileId) const {
                std::lock_guard<std::mutex> lock(_mutex);
                auto it = _fetchingTiles.find(tileId);
                return it != _fetchingTiles.end() ? it->second : std::vector<std::shared_ptr<FetchTaskBase> >();
            }
            
            void insert(long long tileId, const std::shared_ptr<FetchTaskBase>& task) {
                std::lock_guard<std::mutex> lock(_mutex);
                _fetchingTiles[tileId].push_back(task);
            }
            
            void remove(long long tileId, const std::shared_ptr<FetchTaskBase>& task) {
                std::lock_guard<std::mutex> lock(_mutex);
                auto it = _fetchingTiles.find(tileId);
                if (it == _fetchingTiles.end()) {
                    return;
                }
                std::vector<std::shared_ptr<FetchTaskBase> >& tasks = it->second;
                auto it2 = std::find(tasks.begin(), tasks.end(), task);
                if (it2 == tasks.end()) {
                    return;
                }
                tasks.erase(it2);
                if (tasks.empty()) {
                    _fetchingTiles.erase(it);
                }
            }
            
            std::vector<std::shared_ptr<FetchTaskBase> > getAll() const {
                std::lock_guard<std::mutex> lock(_mutex);
                std::vector<std::shared_ptr<FetchTaskBase> > tasks;
                for (auto it = _fetchingTiles.begin(); it != _fetchingTiles.end(); it++) {
                    tasks.insert(tasks.end(), it->second.begin(), it->second.end());
                }
                return tasks;
            }
            
            int getPreloadingCount() const {
                std::lock_guard<std::mutex> lock(_mutex);
                int count = 0;
                for (auto it = _fetchingTiles.begin(); it != _fetchingTiles.end(); it++) {
                    for (const std::shared_ptr<FetchTaskBase>& task : it->second) {
                        if (task->isPreloadingTile()) {
                            count++;
                        }
                    }
                }
                return count;
            }
            
            int getVisibleCount() const {
                std::lock_guard<std::mutex> lock(_mutex);
                int count = 0;
                for (auto it = _fetchingTiles.begin(); it != _fetchingTiles.end(); it++) {
                    for (const std::shared_ptr<FetchTaskBase>& task : it->second) {
                        if (!task->isPreloadingTile()) {
                            count++;
                        }
                    }
                }
                return count;
            }

        private:
            std::unordered_map<long long, std::vector<std::shared_ptr<FetchTaskBase> > > _fetchingTiles;
            mutable std::mutex _mutex;
        };

        explicit TileLayer(const std::shared_ptr<TileDataSource>& dataSource);

        virtual void setComponents(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool,
                                   const std::shared_ptr<CancelableThreadPool>& tileThreadPool,
                                   const std::weak_ptr<Options>& options,
                                   const std::weak_ptr<MapRenderer>& mapRenderer,
                                   const std::weak_ptr<TouchHandler>& touchHandler);

        virtual void loadData(const std::shared_ptr<CullState>& cullState);

        virtual void updateTiles(bool removeTiles);

        virtual void updateTileLoadListener();

        virtual long long getTileId(const MapTile& tile) const = 0;
        virtual bool tileExists(long long tileId, bool preloadingCache) const = 0;
        virtual bool tileValid(long long tileId, bool preloadingCache) const = 0;
        virtual bool prefetchTile(long long tileId, bool preloadingTile) = 0;
        virtual void fetchTile(long long tileId, const MapTile& mapTile, bool preloadingTile, int priorityDelta) = 0;
        virtual void clearTiles(bool preloadingTiles) = 0;
        virtual void invalidateTiles(bool preloadingTiles) = 0;

        virtual void calculateDrawData(const MapTile& visTile, const MapTile& closestTile, bool preloadingTile) = 0;
        // True while calculateDrawData is called for a shadow caster tile (see _shadowCasterTiles).
        bool isCollectingShadowCasters() const { return _collectingShadowCasters; }
        virtual void refreshDrawData(const std::shared_ptr<CullState>& cullState, bool tilesChanged) = 0;
        
        virtual int getMinZoom() const = 0;
        virtual int getMaxZoom() const = 0;
        virtual std::vector<long long> getVisibleTileIds() const = 0;
        
        virtual void calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const;
        virtual bool processClick(const ClickInfo& clickInfo, const RayIntersectedElement& intersectedElement, const ViewState& viewState) const;

        std::shared_ptr<vt::TileTransformer> getTileTransformer() const;
        void resetTileTransformer();

    public:
        /**
         * Marks/unmarks this layer as the terrain depth-write layer. Internal method.
         */
        void setTerrainDepthWriteMode(bool enabled);
        /**
         * Sets the layer stacking order used for terrain depth separation in GPU draping mode. Internal method.
         */
        void setTerrainRenderOrder(int order);

        /**
         * Cross-layer terrain draping. MapRenderer prepares every participating layer's frame,
         * collects the tiles they would drape, bakes them all into one shared texture per tile in
         * layer order, and then draws the terrain surface once. Internal methods.
         */
        virtual void collectDrapeLayers(std::vector<std::shared_ptr<TileLayer> >& drapeLayers, const ViewState& viewState);
        // What this layer contributes to the drape stack's identity: its own address by default,
        // plus - for a layer whose bake does not come from its tiles - whatever its appearance
        // depends on, since it has no per-tile fingerprint to be noticed through.
        virtual std::size_t drapeStackSignature() const;
        // A terrain paint bakes into every tile of the shared drape and reports none: a stack of only such
        // layers needs the terrain's own cover, and every tile of it must expect this layer's content.
        virtual bool paintsEveryDrapeTile() const { return false; }
        // Whether this layer's tiles may hold extrusions, which cast from past the view (see _shadowCasterTiles).
        virtual bool castsExtrusionShadows() const { return false; }
        // The terrain cover a paint layer draws itself on when nothing bakes it. Ignored by
        // layers that are not paints.
        virtual void setTerrainPaintTiles(const std::vector<vt::TileId>& tileIds);

        bool prepareTerrainDrapeFrame(float deltaSeconds, const ViewState& viewState);
        void setExternalDrapeTarget(bool enabled);
        void setExternalDrapeTiles(const std::vector<vt::TileId>& tileIds);
        // The shared terrain ground: the cover every layer of the stack composites onto, drawn
        // once per frame by the front layer (see vt::GLTileRenderer::setTerrainGroundTiles).
        void setTerrainGroundTiles(const std::vector<vt::TileId>& tileIds, const std::vector<int>& proxyDepths);
        // Where this layer's style layers start in the stack's depth ordering: the stack is several renderers,
        // so the owner numbers them in draw order, or a composite's children all claim ordinal 0.
        void setTerrainLayerOrdinalBase(int base);
        int getStyleLayerCount() const;
        int renderTerrainGround(const Color& color);
        void collectDrapeTiles(std::map<vt::TileId, std::size_t>& drapeTiles) const;
        int bakeDrapeTile(const vt::TileId& tileId);
        // The deck's own drape - the span content of a tile, baked apart from the ground's - so a
        // bridge's road lands on the deck carrying it. Only for tiles that have a span at all.
        void collectSpanDrapeTiles(std::map<vt::TileId, std::size_t>& spanTiles) const;
        int bakeSpanDrapeTile(const vt::TileId& tileId);
        void setSpanDrapeTextures(const std::map<vt::TileId, unsigned int>& textures);
        /** A drape tile's composite ground texture and the sub-rect it is drawn through (see MapRenderer). */
        struct GroundDrapeRef {
            unsigned int texture = 0;
            float uvOffsetX = 0.0f, uvOffsetY = 0.0f, uvScale = 1.0f;
        };
        void setGroundDrapeTextures(const std::map<vt::TileId, GroundDrapeRef>& drapes);
        // The ordered draped/live style layers of this layer, for the cross-layer cut (#175).
        void collectDrapeStackOrder(std::vector<std::pair<int, bool> >& units) const;
        int bakeDrapeCoverage(const vt::TileId& tileId, int fromStyleLayerIdx);
        void setDrapeCoverageMasks(const std::vector<std::map<vt::TileId, unsigned int> >& maskTextures, const std::map<int, int>& styleLayerMasks);
        int renderDrapedSurface(const vt::TileId& tileId, unsigned int drapeTexture, float uvOffsetX, float uvOffsetY, float uvScale);
        int renderDrapedSurfaceFill(const vt::TileId& tileId, const Color& color);
        int blitDrapeTexture(unsigned int srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale);
        bool calculateShadowViewProj(const std::vector<vt::TileId>& tileIds, const std::vector<vt::TileId>& casterTileIds, const std::vector<std::pair<double, double> >& casterHeights, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<vt::TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const;
        float shadowCasterFadeSignature(const std::vector<vt::TileId>* coveredBy) const;
        int consumeShadowCastersMissingElevation();
        int renderShadowCasters(const std::vector<vt::TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround);
        void setTerrainShadowMap(unsigned int texture, int mapSize, int cascades, const cglib::vec3<float>& depthBias, const std::array<float, 4>& depthScales, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec2<float>& fadeRange, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, 4>& lightViewProjs);
        void setTerrainShadowMask(unsigned int texture, float invScreenWidth, float invScreenHeight);
        int renderTerrainShadowMask(const std::vector<vt::TileId>& tileIds);
        bool isGroundAOActive() const;
        bool isGroundAOBakeable() const;
        // Whether this layer's visible tiles draw anything on the ground, rather than labels alone.
        bool hasGroundContent() const;
        int renderGroundAOMask();
        int bakeGroundAOMask(const vt::TileId& tileId);
        void setTerrainSunLighting(const ResolvedLighting& lighting);

    protected:

        // The tile zoom the last cull asked for, and how far above its own zoom a coarsened tile may
        // be styled - together they give a fetched tile its style zoom (TileStyleZoom.h).
        int getTargetTileZoom() const { return _targetTileZoom; }
        int getTileStyleZoomLift() const { return _tileStyleZoomLift; }

        // A hook, not an invalidation: tileValid() compares each tile's style zoom stamp, and wiping
        // the caches here re-decoded the whole map on every integer zoom crossing.
        virtual void onTargetTileZoomChanged() { }

        const DirectorPtr<TileDataSource> _dataSource;
        std::shared_ptr<DataSourceListener> _dataSourceListener;

        std::shared_ptr<TileRenderer> _tileRenderer;
    
        FetchingTileTasks _fetchingTileTasks;

        // Tiles fetched unseen so a bridge's chord can resolve - see collectSpanReferenceTiles.
        // The subclass hands their decoded tiles to the renderer from refreshDrawData.
        std::vector<MapTile> _spanReferenceTiles;
        
    private:
        struct FetchTileInfo {
            MapTile tile;
            bool preloading;
            int priorityDelta;
        };

        void calculateVisibleTiles(const std::shared_ptr<CullState>& cullState);
        void calculateVisibleTilesRecursive(const std::shared_ptr<CullState>& cullState, const MapTile& mapTile, const MapBounds& dataExtent);
        void calculateShadowCasterTiles();

        void sortTiles(std::vector<MapTile>& tiles, const ViewState& viewState, bool preloadingTiles);
        void buildFetchTiles(const std::vector<MapTile>& visTiles, bool preloadingTiles, std::vector<FetchTileInfo>& fetchTileList, bool fetchOnly = false);

        bool findParentTile(const MapTile& visTile, const MapTile& tile, int depth, bool preloadingCache, bool preloadingTile);
        int findChildTiles(const MapTile& visTile, const MapTile& tile, int depth, bool preloadingCache, bool preloadingTile);

        static const float DISCRETE_ZOOM_LEVEL_BIAS;
        // Margin past a level boundary before the target tile zoom follows (see calculateTargetTileZoom).
        static const double TARGET_TILE_ZOOM_HYSTERESIS;

        // Ceiling on the terrain tile cover, used to relax the coarsening floor when the
        // view distance would otherwise demand more tiles than a frame can carry.
        static const int TERRAIN_COVER_TILE_BUDGET;

        static const int MAX_PARENT_SEARCH_DEPTH;
        static const int MAX_STAND_IN_DEPTH;
        static const int MAX_CHILD_SEARCH_DEPTH;

        static const int PARENT_PRIORITY_OFFSET;
        static const int PRELOADING_PRIORITY_OFFSET;
        // A stranded span piece's reference tile is fetched this many levels coarser (8x the edge), and a cull asks for so many.
        static const int SPAN_REFERENCE_ZOOM_DROP;
        static const int SPAN_REFERENCE_MIN_ZOOM;
        static const std::size_t MAX_SPAN_REFERENCE_TILES;
        void collectSpanReferenceTiles();
        static const double PRELOADING_TILE_SCALE;
        static const int SHADOW_CASTER_MIN_ZOOM;
        
        std::atomic<bool> _calculatingTiles;
        std::atomic<bool> _refreshedTiles;
        std::atomic<unsigned int> _tileCalculationCount; // completed tile passes, see areVisibleTilesSettledSince
        
        ThreadSafeDirectorPtr<TileDataSource> _utfGridDataSource;
        
        ThreadSafeDirectorPtr<TileLoadListener> _tileLoadListener;
    
        ThreadSafeDirectorPtr<UTFGridEventListener> _utfGridEventListener;

        std::atomic<bool> _synchronizedRefresh;

        int _frameNr;
        int _lastFrameNr;
    
        bool _preloading;
        
        TileSubstitutionPolicy::TileSubstitutionPolicy _substitutionPolicy;
    
        float _zoomLevelBias;
        int _maxOverzoomLevel;
        int _maxStandInLevel;
        int _maxUnderzoomLevel;

        int _targetTileZoom = -1; // the tile zoom the camera asks for, before the LOD coarsens anything
        int _tileStyleZoomLift = 0; // last Options tile style zoom lift a cull ran with
        int _terrainMaxTileZoom = 1000;
        int _terrainMinTileZoom = 0; // terrain mode: the coarsest tile zoom the LOD rule may pick
        double _maxVisibleDistance = 0; // internal units; 0 = as far as the camera can see
        double _lodMaxTileArea = 0; // screen pixels squared; the tangram LOD threshold, 0 = no area test
        double _lodCosThetaExponent = 0; // maplibre's p - 1: extra power on cos(incidence), 0 = the plain area rule
        double _lodZoomOffset = 0; // Options::ZoomOffset, cached per cull: the tile level a zoom targets
        double _lodElevation = 0; // world z the LOD projects a tile at when the DEM has no data for it (the terrain under the focus)
        std::shared_ptr<ElevationManager> _lodElevationManager; // held for one cull pass, per-tile terrain height for the LOD
        bool _terrainOverzoomTargets = false; // terrain mode: target tiles may exceed the data source max zoom (overzoom-fed)

        std::vector<MapTile> _visibleTiles;
        // Outside the viewport but inside the label band (ViewState::getLabelFrustum): fetched and
        // handed to the renderer whatever isPreloading() says, purely so their labels exist in time.
        std::vector<MapTile> _labelTiles;
        std::vector<MapTile> _preloadingTiles;
        // Next to the view on the sun's side, from SHADOW_CASTER_MIN_ZOOM: fetched so their extrusions cast into
        // it, never drawn (mapbox's extendTileCover towards the light).
        std::vector<MapTile> _shadowCasterTiles;
        bool _collectingShadowCasters = false; // calculateDrawData is building _shadowCasterTiles' draw data
        // Sticky once named: rebuilt each cull, a resolved tile would drop out, un-resolve its ends and be named again, forever.
        struct SpanReference {
            MapTile tile;
            unsigned int lastNamed = 0;
        };
        std::vector<SpanReference> _spanReferences;
        unsigned int _spanReferenceCull = 0;
        std::size_t _lastSpanReferenceCount = 0;
        std::unordered_map<MapTile, std::shared_ptr<UTFGridTile> > _utfGridTiles;
        std::shared_ptr<CullState> _tileCullState;

        std::weak_ptr<GLResourceManager> _glResourceManager;
        std::weak_ptr<ProjectionSurface> _projectionSurface;

        std::weak_ptr<TerrainOptions> _terrainOptions;
        bool _terrainEnabled = false;
        int _terrainMeshResolution = 0;
        int _terrainMinZoom = 0;
        bool _terrainSourceDensity = false;
        bool _terrainSourceDensityLines = false;
        float _terrainViewDistanceFactor = 0.0f; // last TerrainOptions view distance factor a cull ran with
        float _tileLODFactor = 0.0f; // last Options tile LOD factor a cull ran with
        int _terrainCoarsening = -1; // last TerrainOptions coarsening bound a cull ran with
        bool _terrainActive = false; // last TerrainOptions active state a cull ran with
        // Its own mutex: the render thread reads this every frame while the switch waits, and
        // _mutex is held for a whole cull.
        mutable std::mutex _terrainDecodeMutex;
        TerrainDecodeWait _terrainDecodeWait;
    };
    
}

#endif

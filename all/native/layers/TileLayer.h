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
         * Sets the state of preloading for this layer. Preloading allows the downloading of tiles that are not
         * currently visible on screen, but are adjacent to ones that are. This means that the user can pan the map without
         * immediately noticing any missing tiles.
         *
         * Enabling this option might introduce a small performance hit on slower devices. It should also be noted that this
         * will considerably increase network traffic if used with online maps. The default is false.
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
         * Sets how many zoom levels up a cached tile may stand in for a missing one while it loads.
         *
         * This used to be MaxOverzoomLevel, which is a different thing: that one says how far the
         * SDK may go for the DATA of a tile the source does not have, and it has to stay deep. The
         * stand-in is only what is shown meanwhile, and a deep one is visible as a ladder - the same
         * area redrawn from a z7 tile, then z8, then z9, each magnified 2^N and so 2^N coarser than
         * it was meant to look. That is glaring for a source whose detail changes with zoom, like
         * generated contours, and merely wasteful for a raster.
         *
         * The default is 6, the same as MaxOverzoomLevel: a zoom-in of several levels must still
         * find something to show, or the map goes empty exactly when the user asked for more detail.
         * Lower it (1 = immediate parent only) for a source whose look changes so much with zoom that
         * a coarse stand-in is worse than nothing.
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
        bool isTerrainDecodeSettled();

    protected:
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
        // What this layer contributes to the drape stack's identity. The layer's own address by
        // default - a new layer object means new content - plus, for layers whose bake does not
        // come from their tiles (a terrain paint), whatever their appearance depends on: they
        // have no per-tile fingerprint through which a change could be noticed.
        virtual std::size_t drapeStackSignature() const;
        // Whether this layer's drape contribution is not made of tiles: a terrain paint bakes into
        // EVERY tile of the shared drape and reports none of them. The owner needs both facts - a
        // stack of nothing but such layers has to be given the terrain's own cover, and every tile
        // of that cover must expect this layer's content or a tile baked without it looks finished.
        virtual bool paintsEveryDrapeTile() const { return false; }
        // The terrain cover a paint layer draws itself on when nothing bakes it. Ignored by
        // layers that are not paints.
        virtual void setTerrainPaintTiles(const std::vector<vt::TileId>& tileIds);

        bool prepareTerrainDrapeFrame(float deltaSeconds, const ViewState& viewState);
        void setExternalDrapeTarget(bool enabled);
        void setExternalDrapeTiles(const std::vector<vt::TileId>& tileIds);
        // The shared terrain ground: the cover every layer of the stack composites onto, drawn
        // once per frame by the front layer (see vt::GLTileRenderer::setTerrainGroundTiles).
        void setTerrainGroundTiles(const std::vector<vt::TileId>& tileIds, const std::vector<int>& proxyDepths);
        // Where this layer's style layers start in the stack's depth ordering. Tangram has ONE
        // ordered style list; our stack is several renderers, so the owner numbers them in draw
        // order - without it a composite's children all claim ordinal 0 and the base map's fills
        // are pulled in front of the hillshade above them.
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
        // The ordered draped/live style layers of this layer, for the cross-layer cut (#175).
        void collectDrapeStackOrder(std::vector<std::pair<int, bool> >& units) const;
        int bakeDrapeCoverage(const vt::TileId& tileId, int fromStyleLayerIdx);
        void setDrapeCoverageMasks(const std::vector<std::map<vt::TileId, unsigned int> >& maskTextures, const std::map<int, int>& styleLayerMasks);
        int renderDrapedSurface(const vt::TileId& tileId, unsigned int drapeTexture, float uvOffsetX, float uvOffsetY, float uvScale);
        int renderDrapedSurfaceFill(const vt::TileId& tileId, const Color& color);
        int blitDrapeTexture(unsigned int srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale);
        bool calculateShadowViewProj(const std::vector<vt::TileId>& tileIds, const std::vector<vt::TileId>& casterTileIds, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<vt::TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const;
        float shadowCasterFadeSignature() const;
        int consumeShadowCastersMissingElevation();
        int renderShadowCasters(const std::vector<vt::TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround);
        void setTerrainShadowMap(unsigned int texture, int mapSize, int cascades, const std::array<float, 4>& depthBiases, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, 4>& lightViewProjs);
        void setTerrainShadowMask(unsigned int texture, float invScreenWidth, float invScreenHeight);
        int renderTerrainShadowMask(const std::vector<vt::TileId>& tileIds);
        bool isGroundAOActive() const;
        bool isGroundAOBakeable() const;
        void setLabelOcclusionDepth(unsigned int depthTexture, float occluderSize);
        bool isLabelOcclusionWanted() const;
        int renderLabelOcclusionDepth();
        int renderGroundAOMask();
        int bakeGroundAOMask(const vt::TileId& tileId);
        void setTerrainSunLighting(const ResolvedLighting& lighting);

    protected:

        const DirectorPtr<TileDataSource> _dataSource;
        std::shared_ptr<DataSourceListener> _dataSourceListener;

        std::shared_ptr<TileRenderer> _tileRenderer;
    
        FetchingTileTasks _fetchingTileTasks;
        
    private:
        struct FetchTileInfo {
            MapTile tile;
            bool preloading;
            int priorityDelta;
        };

        void calculateVisibleTiles(const std::shared_ptr<CullState>& cullState);
        void calculateVisibleTilesRecursive(const std::shared_ptr<CullState>& cullState, const MapTile& mapTile, const MapBounds& dataExtent);

        void sortTiles(std::vector<MapTile>& tiles, const ViewState& viewState, bool preloadingTiles);
        void buildFetchTiles(const std::vector<MapTile>& visTiles, bool preloadingTiles, std::vector<FetchTileInfo>& fetchTileList);

        bool findParentTile(const MapTile& visTile, const MapTile& tile, int depth, bool preloadingCache, bool preloadingTile);
        int findChildTiles(const MapTile& visTile, const MapTile& tile, int depth, bool preloadingCache, bool preloadingTile);

        static const float DISCRETE_ZOOM_LEVEL_BIAS;

        // Ceiling on the terrain tile cover, used to relax the coarsening floor when the
        // view distance would otherwise demand more tiles than a frame can carry.
        static const int TERRAIN_COVER_TILE_BUDGET;

        static const int MAX_PARENT_SEARCH_DEPTH;
        static const int MAX_STAND_IN_DEPTH;
        static const int MAX_CHILD_SEARCH_DEPTH;

        static const int PARENT_PRIORITY_OFFSET;
        static const int PRELOADING_PRIORITY_OFFSET;
        static const double PRELOADING_TILE_SCALE;
        
        std::atomic<bool> _calculatingTiles;
        std::atomic<bool> _refreshedTiles;
        
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

        int _terrainMaxTileZoom = 1000;
        int _terrainMinTileZoom = 0; // terrain mode: the coarsest tile zoom the LOD rule may pick
        double _maxVisibleDistance = 0; // internal units; 0 = as far as the camera can see
        double _lodMaxTileArea = 0; // screen pixels squared; the tangram LOD threshold, 0 = no area test
        double _lodMinCosTheta = 0; // cos of the most grazing incidence the LOD charges for, 0 = no limit
        double _lodElevation = 0; // world z the LOD projects a tile at when the DEM has no data for it (the terrain under the focus)
        std::shared_ptr<ElevationManager> _lodElevationManager; // held for one cull pass, per-tile terrain height for the LOD
        bool _terrainOverzoomTargets = false; // terrain mode: target tiles may exceed the data source max zoom (overzoom-fed)

        std::vector<MapTile> _visibleTiles;
        std::vector<MapTile> _preloadingTiles;
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
        std::atomic<bool> _terrainDecodeSettled { true }; // false while a 2D/3D switch's tiles are on their way
    };
    
}

#endif

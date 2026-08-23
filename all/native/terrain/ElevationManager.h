/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ELEVATIONMANAGER_H_
#define _MASSIF_ELEVATIONMANAGER_H_

#include "components/ElevationProvider.h"
#include "core/MapPos.h"
#include "core/MapTile.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <utility>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <stdext/timed_lru_cache.h>

namespace massif {
    class TileDataSource;
    class ElevationDecoder;
    class ElevationTileGrid;
    class Projection;

    /**
     * Manages decoded DEM elevation grids on top of a raster elevation tile data source
     * (the same data source that can be simultaneously used by HillshadeRasterTileLayer).
     * Provides thread-safe elevation lookups in meters and in display units
     * (internal z units, including exaggeration and Mercator latitude scale),
     * ray intersection against the displaced terrain surface, and per-tile elevation bounds.
     * Internal class, not exposed in the public API.
     */
    class ElevationManager : public ElevationProvider {
    public:
        // Texels of elevation data per unit of tile size, i.e. the density tangram's zoom bias
        // normalises every raster source to: 256 texels over a 256-point tile is one texel per
        // point, so a 512-texel source is used one zoom level coarser.
        static constexpr int DEM_TEXELS_PER_TILE_UNIT = 256;

        enum class LoadMode {
            /**
             * Only already decoded grids (or their cached ancestors) may be used. Never blocks.
             */
            CACHED_ONLY,
            /**
             * The elevation tile may be synchronously loaded from the data source. May block on IO/network.
             */
            ALLOW_LOAD,
            /**
             * Like ALLOW_LOAD, but a cached ancestor is not accepted as a stand-in: the tile itself
             * is loaded unless the data source has already answered that this level does not exist.
             * May block on IO/network.
             */
            LOAD_EXACT
        };

        ElevationManager(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& elevationDecoder);
        virtual ~ElevationManager();

        std::shared_ptr<TileDataSource> getDataSource() const;
        std::shared_ptr<ElevationDecoder> getElevationDecoder() const;

        float getExaggeration() const;
        void setExaggeration(float exaggeration);

        bool isSeamlessTileEdgesEnabled() const;
        void setSeamlessTileEdgesEnabled(bool enabled);

        bool isNeighbourPrefetchEnabled() const;
        void setNeighbourPrefetchEnabled(bool enabled);

        /**
         * Sets the terrain surface resolution (mesh cells per tile edge). Elevation levels are
         * capped so that one elevation texel covers at most half a surface cell: finer data cannot
         * be expressed by the mesh, but every level costs four times the tiles, decoded grids and
         * GL textures. The cap applies to EVERY elevation query, so the displaced surface, the
         * elevation lookups, the ray intersection used for billboard occlusion and the CPU-side
         * element placement all agree on one height field.
         */
        void setSurfaceResolution(int resolution);

        std::size_t getCacheCapacity() const;
        void setCacheCapacity(std::size_t capacityInBytes);

        /**
         * Returns the elevation in meters at the given WGS84 position, loading the elevation tile if needed.
         * Matches HillshadeRasterTileLayer::getElevation semantics (returns -1000000 if no data is available).
         */
        double getElevation(const MapPos& pos) const;
        /**
         * Batch version of getElevation. One elevation value is returned for every input position, in order.
         */
        std::vector<double> getElevations(const std::vector<MapPos>& poses) const;

        /**
         * Returns the elevation in meters at the given internal coordinates. Returns 0 if no data is available.
         */
        double getElevationMeters(double internalX, double internalY, LoadMode mode) const;
        /**
         * Returns the display height (internal z units, including exaggeration and Mercator scale)
         * at the given internal coordinates. Returns 0 if no data is available.
         */
        double getDisplayHeight(double internalX, double internalY, LoadMode mode) const;
        /**
         * Returns the display height gradient (dz/dx, dz/dy, unitless) at the given internal coordinates.
         */
        void getDisplayGradient(double internalX, double internalY, LoadMode mode, double& dhdx, double& dhdy) const;

        /**
         * Returns the decoded elevation grid covering the given RENDER tile (the tile zoom is
         * mapped to the elevation level by getDataTile and cached ancestors act as fallbacks).
         * May return null.
         * The tile must be in XYZ convention (y=0 north, same as vt::TileId and TileDataSource::loadTile).
         */
        std::shared_ptr<ElevationTileGrid> getTileGrid(const MapTile& mapTile, LoadMode mode) const;

        /**
         * Like getTileGrid, but the tile is an ELEVATION tile (a getDataTile result or one of its
         * neighbours), not a render tile: only the data source zoom range is applied to it. Passing
         * an already-resolved elevation tile to getTileGrid would map it down a second time, which
         * costs one elevation level per hop.
         */
        std::shared_ptr<ElevationTileGrid> getDataTileGrid(const MapTile& dataTile, LoadMode mode) const;

        /**
         * Returns the tile that actually carries the elevation data for the given render tile: the
         * same tile, or an ancestor - capped by the data source maximum zoom level and by the
         * resolution the terrain mesh can express.
         */
        MapTile getDataTile(const MapTile& mapTile) const;

        /**
         * Returns the tile carrying the elevation data for the given render tile at FULL detail:
         * capped by the data source maximum zoom level only, not by what the terrain mesh can
         * express. For consumers that resolve more than the mesh does - shading is per fragment,
         * so it shows relief the surface geometry could never carry.
         */
        MapTile getFullDetailDataTile(const MapTile& mapTile) const;
        /**
         * The elevation tile for a consumer that resolves 'extraLevels' more detail than the
         * terrain MESH can express. The mesh cap (one texel per half surface cell) is right for
         * geometry and wrong for per-fragment shading, which resolves far more - see
         * getFullDetailDataTile for the extreme. extraLevels 0 is the mesh cap itself.
         */
        MapTile getDetailDataTile(const MapTile& mapTile, int extraLevels) const;

        /**
         * Requests an asynchronous load of the given ELEVATION tile (as returned by getDataTile,
         * or one of its neighbours - it is not mapped down again). Never blocks and never
         * performs IO on the calling thread. A no-op if the grid is already cached, already
         * queued or currently being loaded, or if neighbour prefetching is disabled.
         * Priority 2 (the tile's own elevation level) is served before 1 (edge neighbours),
         * which is served before 0 (diagonal neighbours, which only fill a corner texel).
         * The tile must be in XYZ convention (y=0 north, same as getTileGrid).
         */
        void prefetchTileGrid(const MapTile& dataTile, int priority) const;

        /**
         * Returns the meters-to-internal-display-units scale at the given internal y coordinate
         * (Mercator latitude correction included, exaggeration not included).
         */
        double getDisplayScale(double internalY) const;

        /**
         * Returns the conservative global display height range (internal z units) using
         * the latitude scale at the given internal y coordinate.
         */
        void getDisplayHeightRange(double internalY, double& minZ, double& maxZ) const;

        virtual double getDisplayHeight(double internalX, double internalY) const override;
        virtual bool intersectRay(const cglib::ray3<double>& ray, double& t) const override;
        /**
         * The tile must be in XYZ convention (y=0 north, same as vt::TileId).
         */
        virtual void getMinMaxDisplayHeight(const MapTile& tile, double& minZ, double& maxZ) const override;
        /**
         * Like getMinMaxDisplayHeight, but without the conservative clamp that always includes
         * sea level. Only valid where a loose range costs more than a missing one - fitting a
         * shadow box, not culling. Falls back to the clamped range when the tile has no data.
         */
        void getMinMaxDisplayHeightExact(const MapTile& tile, double& minZ, double& maxZ) const;
        /**
         * getMinMaxDisplayHeightExact, plus whether the range came from decoded data: false when
         * the tile has no cached grid, for a caller that would rather keep its own estimate than
         * use the conservative global one. Never loads.
         */
        bool getMinMaxDisplayHeightCached(const MapTile& tile, double& minZ, double& maxZ) const;
        virtual unsigned int getVersion() const override;

        /**
         * Appends the elevation tiles whose data changed between 'sinceVersion' (exclusive) and
         * the current version to 'tiles', in XYZ convention. Returns false if the change log no
         * longer reaches back to 'sinceVersion' (log overflow, or the whole cache was dropped):
         * the caller must then treat every tile as changed.
         */
        bool getChangedTiles(unsigned int sinceVersion, std::vector<MapTile>& tiles) const;

        /**
         * Called on a tile-loading thread whenever decoded elevation data changed. Nothing polls
         * for it: the consumers all read the version from inside a frame, so without a redraw
         * request the map goes idle on whatever mesh the last frame happened to have and only
         * catches up on the next gesture.
         */
        void setDataChangedListener(const std::function<void()>& listener);

        /**
         * Resolves the effective elevation decoder: the data source "encoding" setting takes precedence,
         * then the preferred decoder, then the MapBox decoder as the default.
         */
        static std::shared_ptr<ElevationDecoder> ResolveDecoder(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& preferredDecoder);

    private:
        struct DataSourceListener;

        static unsigned long long NextInstanceId();

        /**
         * The version of the elevation DATA alone - every change except a scale-only one. The
         * exaggeration scales heights on the GPU and does not touch the tile surfaces, which are
         * built flat, so a consumer that only rebuilds geometry watches this instead of the global
         * version and is not woken by an exaggeration ramp.
         */
    public:
        unsigned int getDataVersion() const;
    private:

        void tilesChanged();
        void bumpGlobalVersion();
        void notifyDataChanged() const;
        double wrapInternalX(double internalX) const;
        MapTile clampTileZoom(const MapTile& mapTile) const;
        MapTile clampDataTileZoom(const MapTile& dataTile) const;
        std::shared_ptr<ElevationTileGrid> lookupTileGrid(const MapTile& dataTile, LoadMode mode) const;
        std::shared_ptr<ElevationTileGrid> getGridForInternalPos(double internalX, double internalY, LoadMode mode) const;
        std::shared_ptr<ElevationTileGrid> loadTileGrid(const MapTile& mapTile) const;
        bool getMinMaxDisplayHeight(const MapTile& tile, double& minZ, double& maxZ, bool exact) const;
        void runPrefetchWorker() const;

        const std::shared_ptr<TileDataSource> _dataSource;
        const std::shared_ptr<ElevationDecoder> _elevationDecoder;
        const std::shared_ptr<Projection> _projection;
        std::shared_ptr<DataSourceListener> _dataSourceListener;

        // Process-unique, so that the per-thread grid memo in lookupTileGrid can tell two
        // managers apart without comparing addresses (a destroyed manager's address can be
        // handed straight back to the next one).
        const unsigned long long _instanceId;

        mutable std::atomic<unsigned int> _dataVersion; // moves with _version whenever tile data changes

        std::atomic<float> _exaggeration;
        std::atomic<bool> _seamlessTileEdges;
        std::atomic<int> _surfaceResolution;      // terrain mesh cells per tile edge
        mutable std::atomic<int> _gridSizeHint;   // texels per elevation tile edge, from the last decoded grid
        std::atomic<bool> _neighbourPrefetch;
        mutable std::atomic<unsigned int> _version;
        mutable std::atomic<float> _maxSeenElevation;

        // Which tiles changed at which version, so that consumers holding per-tile derived
        // data (tile surfaces) can invalidate only what actually changed instead of
        // everything on every decoded elevation tile.
        static constexpr std::size_t MAX_CHANGE_LOG_ENTRIES = 256;
        mutable std::deque<std::pair<unsigned int, MapTile> > _changeLog;
        mutable unsigned int _changeLogFirstVersion = 1; // earliest version still covered by the log

        mutable cache::timed_lru_cache<long long, std::shared_ptr<ElevationTileGrid> > _gridCache;
        bool _gridCacheCapacityFixed = false; // set through setCacheCapacity: the app's number wins over the grid-count rule
        mutable std::map<long long, std::shared_future<std::shared_ptr<ElevationTileGrid> > > _pendingLoads; // single-flight de-duplication of concurrent loads
        std::function<void()> _dataChangedListener; // called outside _mutex, see setDataChangedListener
        mutable std::mutex _mutex;

        // Background prefetch worker: loads elevation tiles requested by the render thread
        // (visible tiles + their neighbours) without ever blocking it. The thread is started
        // on the first request and joined in the destructor.
        mutable std::deque<MapTile> _prefetchQueue;      // low priority (neighbour borders)
        mutable std::deque<MapTile> _prefetchQueueHigh;  // high priority (the tile's own level)
        mutable std::set<long long> _prefetchTileIds;
        mutable std::vector<std::thread> _prefetchThreads;
        mutable std::condition_variable _prefetchCondition;
        mutable bool _prefetchStopped;
        mutable std::mutex _prefetchMutex;
    };
}

#endif

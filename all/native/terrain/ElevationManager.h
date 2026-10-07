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
#include <vt/RenderStats.h> // MASSIF_VT_RENDER_STATS gates the diagnostic member below

namespace massif {
    class TileDataSource;
    class ElevationDecoder;
    class ElevationTileGrid;
    class Projection;

    /**
     * Decoded DEM grids over a raster elevation data source: thread-safe heights in meters and display units
     * (internal z, exaggeration and Mercator latitude scale included), ray intersection and per-tile bounds.
     * Internal class, not exposed in the public API.
     */
    class ElevationManager : public ElevationProvider {
    public:
        // Texels per 256-point tile that tangram's zoom bias normalises every raster source to:
        // a 512-texel source is used one zoom level coarser.
        static constexpr int DEM_TEXELS_PER_TILE_UNIT = 256;

        enum class LoadMode {
            /**
             * Only already decoded grids (or their cached ancestors) may be used. Never blocks.
             */
            CACHED_ONLY,
            /**
             * Only the tile itself, or the grid the data source answered it with. Never blocks.
             */
            CACHED_EXACT,
            /**
             * The elevation tile may be synchronously loaded from the data source. May block on IO/network.
             */
            ALLOW_LOAD,
            /**
             * Like ALLOW_LOAD, but no cached ancestor stands in: the tile itself is loaded unless the
             * data source has already answered that this level does not exist. May block on IO/network.
             */
            LOAD_EXACT
        };

        ElevationManager(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& elevationDecoder);
        virtual ~ElevationManager();

        std::shared_ptr<TileDataSource> getDataSource() const;
        /** The source-level default. Each tile resolves its own decoder from its "dem_encoding". */
        std::shared_ptr<ElevationDecoder> getElevationDecoder() const;

        float getExaggeration() const;
        void setExaggeration(float exaggeration);

        bool isSeamlessTileEdgesEnabled() const;
        void setSeamlessTileEdgesEnabled(bool enabled);

        bool isNeighbourPrefetchEnabled() const;
        void setNeighbourPrefetchEnabled(bool enabled);

        /**
         * Caps the zoom of the elevation tiles resolved; 0 (default) = the source maximum. A working-set
         * control: keeps a far-reaching view within the grid cache so it stops thrashing.
         * Driven by TerrainOptions::setMaxZoom. Changing it drops the decoded grids.
         */
        int getMaxDataZoomCap() const;
        void setMaxDataZoomCap(int maxZoom);

        /**
         * Whether display heights read the DEM bilinearly rather than its node field, to match a
         * bilinear mesh (TerrainOptions::setSubdivideDistance). Changing it bumps the version.
         */
        bool isBilinearSurface() const;
        void setBilinearSurface(bool bilinear);

        /**
         * Sets the terrain surface resolution (mesh cells per tile edge). Each grid's node field is built for it,
         * and both the surface and every display-height query here read that field, so they agree.
         * Changing it drops the decoded grids.
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
         * The skyline from a viewpoint: per azimuth (degrees clockwise from north), the apparent altitude
         * in degrees of the highest terrain, with curvature and refraction. Non-blocking: missing grids are requested.
         * @param pos The viewpoint, WGS84.
         * @param eyeHeight Metres above the ground at the viewpoint.
         * @param maxDistance How far out, in metres.
         * @return One altitude per azimuth, in degrees; -90 where no terrain is known.
         */
        std::vector<double> calculateHorizon(const MapPos& pos, double eyeHeight, const std::vector<double>& azimuths, double maxDistance) const;

        /**
         * Returns the elevation in meters at the given internal coordinates, from the DEM itself.
         * Returns 0 if no data is available.
         */
        double getElevationMeters(double internalX, double internalY, LoadMode mode) const;
        /**
         * Returns the display height (internal z, exaggeration and Mercator scale included) of the drawn
         * surface, i.e. the node field, not the DEM. Returns 0 if no data is available.
         */
        double getDisplayHeight(double internalX, double internalY, LoadMode mode) const;
        /**
         * The same, but tells "no data" apart from sea level (getDisplayHeight returns 0 for both),
         * for a caller that bakes the answer into geometry. Leaves height untouched when no grid covers the point.
         * @return True if a cached grid answered.
         */
        bool getDisplayHeightCached(double internalX, double internalY, double& height) const;
        /**
         * The same, also reporting the zoom of the grid that answered, which may be a coarser cached ancestor.
         * @return True if a cached grid answered.
         */
        bool getDisplayHeightCached(double internalX, double internalY, double& height, int& resolvedZoom) const;
        /**
         * Returns the display height gradient (dz/dx, dz/dy, unitless) at the given internal coordinates.
         */
        void getDisplayGradient(double internalX, double internalY, LoadMode mode, double& dhdx, double& dhdy) const;

        /**
         * Returns the decoded grid covering the given render tile (mapped by getDataTile, cached ancestors
         * as fallbacks), or null. The tile must be in XYZ convention (y=0 north, as vt::TileId).
         */
        std::shared_ptr<ElevationTileGrid> getTileGrid(const MapTile& mapTile, LoadMode mode) const;

        /**
         * Like getTileGrid, but for an elevation tile (a getDataTile result or a neighbour): only the source
         * zoom range is applied. getTileGrid would map it down again, one elevation level per hop.
         */
        std::shared_ptr<ElevationTileGrid> getDataTileGrid(const MapTile& dataTile, LoadMode mode) const;

        /**
         * Returns the tile that actually carries the elevation data for the given render tile: the
         * same tile, or an ancestor - capped by the data source maximum zoom level and by the
         * resolution the terrain mesh can express.
         */
        MapTile getDataTile(const MapTile& mapTile) const;

        /**
         * The render tile covering an internal position at the source maximum zoom. With getDataTile
         * and prefetchTileGrid, loads a point no visible tile covers (e.g. the ground under a low camera).
         */
        MapTile getTileForInternalPos(double internalX, double internalY) const;

        /**
         * The render tile zoom past which this DEM adds no relief: the source maximum plus the levels
         * clampTileZoom drops for an oversized grid. Where the terrain LOD floor stops following the camera.
         */
        int getDetailZoomLimit() const;

        /**
         * The elevation tile for the given render tile at full detail, capped by the source maximum zoom only,
         * for consumers that resolve more than the mesh (per-fragment shading).
         */
        MapTile getFullDetailDataTile(const MapTile& mapTile) const;
        /**
         * The elevation tile for a consumer that resolves 'extraLevels' more than the mesh cap (one texel per
         * half surface cell). extraLevels 0 is the mesh cap itself.
         */
        MapTile getDetailDataTile(const MapTile& mapTile, int extraLevels) const;

        /**
         * Queues an async load of the given elevation tile (XYZ, not mapped down again); never blocks. No-op if
         * cached, queued, loading, or neighbour prefetching is off. Priority 2 (the tile's own level) is served
         * before 1 (edge neighbours), then 0 (diagonals, which only fill a corner texel).
         */
        void prefetchTileGrid(const MapTile& dataTile, int priority) const;

        /** The same queueing for a tile a consumer needs; ignores TerrainOptions::ElevationPrefetchEnabled. */
        void requestTileGrid(const MapTile& dataTile, int priority) const;
        /** Whether the elevation tile is queued or loading: a failure, a shed request or a landed grid is not. */
        bool isTileGridPending(const MapTile& dataTile) const;

        /**
         * Requests the finest elevation tile under a point unless 'resolvedZoom' (a cached read's, -1 for none)
         * already is it. A cached read only finds what others loaded; off screen, nothing else loads it.
         */
        void requestTileGridAt(double internalX, double internalY, int resolvedZoom, int priority) const;

        /**
         * Sets the camera focus (internal coordinates) the prefetch queue drains nearest-first from, within a
         * priority. Read at dequeue, so a pan re-orders what is already waiting. Until set, newest first.
         */
        void setPrefetchFocus(double internalX, double internalY) const;

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
        /**
         * The same range at an explicit exaggeration rather than the current one. The auto-flatten
         * criterion needs the range the terrain WOULD have, which the flatten ramp has scaled away.
         */
        void getDisplayHeightRange(double internalY, float exaggeration, double& minZ, double& maxZ) const;

        virtual double getDisplayHeight(double internalX, double internalY) const override;
        virtual void getDisplayGradient(double internalX, double internalY, double& dhdx, double& dhdy) const override;
        virtual int getMaxDataZoom() const override;
        virtual bool intersectRay(const cglib::ray3<double>& ray, double& t) const override;
        /**
         * Whether loaded terrain blocks the first maxFraction of the segment between two display-space
         * points. Unlike intersectRay, the segment may climb.
         */
        bool isSegmentBlocked(const cglib::vec3<double>& from, const cglib::vec3<double>& to, double maxFraction) const;
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
         * getMinMaxDisplayHeightExact, plus whether the range came from decoded data (false without a cached
         * grid), for a caller that would rather keep its own estimate. Never loads.
         */
        bool getMinMaxDisplayHeightCached(const MapTile& tile, double& minZ, double& maxZ) const;
        virtual unsigned int getVersion() const override;

        /**
         * Appends the elevation tiles (XYZ) changed since 'sinceVersion' (exclusive). Returns false if the log
         * no longer reaches back that far (overflow, or the cache was dropped): treat every tile as changed.
         */
        bool getChangedTiles(unsigned int sinceVersion, std::vector<MapTile>& tiles) const;

        /**
         * Called on a tile-loading thread whenever decoded elevation data changed. Consumers only read the
         * version inside a frame, so without this redraw request the map idles on a stale mesh.
         */
        void setDataChangedListener(const std::function<void()>& listener);

    private:
        struct DataSourceListener;

        static unsigned long long NextInstanceId();

        /**
         * The version of the elevation data alone, not bumped by an exaggeration change (scaled on the GPU,
         * surfaces built flat), so a geometry-only consumer is not woken by an exaggeration ramp.
         */
    public:
        unsigned int getDataVersion() const;
    private:

        void tilesChanged();
        float sampleSurfaceHeight(const ElevationTileGrid& grid, double internalX, double internalY) const;
        void bumpGlobalVersion();
        void notifyDataChanged() const;
        double wrapInternalX(double internalX) const;
        MapTile clampTileZoom(const MapTile& mapTile) const;
        MapTile clampDataTileZoom(const MapTile& dataTile) const;
        /** The source maximum, or the MaxDataZoom cap where one is set and is lower. */
        int dataMaxZoom() const;
        static int nodeBoxCells();
        /** Cache read that honours the failure marker's expiry, which read() alone does not. */
        bool readCachedGrid(long long tileId, std::shared_ptr<ElevationTileGrid>& grid) const;
        std::shared_ptr<ElevationTileGrid> lookupTileGrid(const MapTile& dataTile, LoadMode mode) const;
        std::shared_ptr<ElevationTileGrid> getGridForInternalPos(double internalX, double internalY, LoadMode mode) const;
        std::shared_ptr<ElevationTileGrid> loadTileGrid(const MapTile& mapTile) const;
        bool getMinMaxDisplayHeight(const MapTile& tile, double& minZ, double& maxZ, bool exact) const;
        void runPrefetchWorker() const;

        const std::shared_ptr<TileDataSource> _dataSource;
        const std::shared_ptr<ElevationDecoder> _elevationDecoder;
        const std::shared_ptr<Projection> _projection;
        std::shared_ptr<DataSourceListener> _dataSourceListener;

        // Process-unique, so the per-thread grid memo tells managers apart; an address can be reused.
        const unsigned long long _instanceId;

        mutable std::atomic<unsigned int> _dataVersion; // moves with _version whenever tile data changes

        std::atomic<float> _exaggeration;
        std::atomic<bool> _seamlessTileEdges;
        std::atomic<int> _surfaceResolution;      // terrain mesh cells per tile edge
        mutable std::atomic<int> _gridSizeHint;   // texels per elevation tile edge, from the last decoded grid
        std::atomic<int> _maxDataZoom;            // setMaxDataZoomCap; 0 = the source maximum
        std::atomic<bool> _bilinearSurface;       // setBilinearSurface
        std::atomic<bool> _neighbourPrefetch;
        mutable std::atomic<unsigned int> _version;
        mutable std::atomic<float> _maxSeenElevation;

        // Which tiles changed at which version, so per-tile consumers invalidate only what changed.
        static constexpr std::size_t MAX_CHANGE_LOG_ENTRIES = 256;
        mutable std::deque<std::pair<unsigned int, MapTile> > _changeLog;
        mutable unsigned int _changeLogFirstVersion = 1; // earliest version still covered by the log

        mutable cache::timed_lru_cache<long long, std::shared_ptr<ElevationTileGrid> > _gridCache;
        bool _gridCacheCapacityFixed = false; // set through setCacheCapacity: the app's number wins over the grid-count rule
#if MASSIF_VT_RENDER_STATS
        mutable std::set<long long> _everLoadedTiles; // diagnostics only: tells an eviction reload from a first load
#endif
        mutable std::map<long long, std::shared_future<std::shared_ptr<ElevationTileGrid> > > _pendingLoads; // single-flight de-duplication of concurrent loads
        std::function<void()> _dataChangedListener; // called outside _mutex, see setDataChangedListener
        mutable std::mutex _mutex;

        // Prefetch workers: started on the first request, joined in the destructor. Drained by priority, then distance.
        struct PrefetchEntry {
            MapTile tile;
            int priority;
        };

        mutable std::deque<PrefetchEntry> _prefetchQueue;      // low priority (neighbour borders)
        mutable std::deque<PrefetchEntry> _prefetchQueueHigh;  // high priority (the tile's own level)
        // Normalised mercator (see setPrefetchFocus). Plain atomics: a torn pair only mis-ranks one tile.
        mutable std::atomic<double> _prefetchFocusU;
        mutable std::atomic<double> _prefetchFocusV;
        mutable std::atomic<bool> _prefetchFocusValid;
        mutable std::set<long long> _prefetchTileIds;
        mutable std::vector<std::thread> _prefetchThreads;
        mutable std::condition_variable _prefetchCondition;
        mutable bool _prefetchStopped;
        mutable std::mutex _prefetchMutex;
    };
}

#endif

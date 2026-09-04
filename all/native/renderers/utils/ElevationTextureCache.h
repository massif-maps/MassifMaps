/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ELEVATIONTEXTURECACHE_H_
#define _MASSIF_ELEVATIONTEXTURECACHE_H_

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "core/MapTile.h"
#include "terrain/ElevationTileGrid.h" // BorderStrips is a member of a queued patch

#include <vt/GLTileRenderer.h>

namespace massif {
    class Bitmap;
    class ElevationManager;
    class ElevationTileGrid;
    class GLResourceManager;
    class Texture;

    /**
     * GL elevation texture cache for GPU terrain draping: implements the
     * vt::GLTileRenderer terrain texture provider on top of the ElevationManager
     * grid cache. Textures are keyed by the grid's own tile, so overzoomed tiles
     * and all tile layers share one texture per DEM tile, and neighbouring tiles
     * sampling the same DEM level sample one continuous texture.
     *
     * A texture is PREPARED before it is used, never built inside the frame that first
     * samples it (tangram gets this for free: its elevation raster is the tile's own texture,
     * created when the tile loads). Encoding the padded texture runs on a worker thread, and
     * the upload runs on the GL thread under a per-frame budget - measured on a Crosscall, an
     * encode+upload in the middle of the frame that samples it cost 45 ms + 52 ms for one
     * 514x514 texture, which is most of a frame per tile.
     *
     * Must be used from the GL thread only (except the worker, which touches nothing else).
     * Internal class, not exposed in the public API.
     */
    class ElevationTextureCache {
    public:
        ElevationTextureCache(const std::shared_ptr<ElevationManager>& elevationManager, const std::shared_ptr<GLResourceManager>& glResourceManager);
        ~ElevationTextureCache();

        const std::shared_ptr<ElevationManager>& getElevationManager() const { return _elevationManager; }

        /**
         * Fills the terrain texture info for the given tile using the best cached
         * elevation grid (the grid may cover an ancestor tile). Returns false when no texture
         * is ready yet - the encode is queued and the tile renders flat (or from an ancestor
         * grid) until it is, exactly as it does while the elevation data itself is loading.
         */
        bool getTexture(const vt::TileId& tileId, vt::GLTileRenderer::TerrainTexture& terrainTexture);

        /**
         * Starts a new frame: uploads what the worker has encoded (up to the frame's budget) and
         * drops the per-frame tile resolution memo. The provider is called once per tile per
         * render pass, so without the memo every pass would redo the grid and neighbour lookups
         * (9 locked cache lookups per tile) for the same result.
         */
        void beginFrame();

        /**
         * Ground height at an internal position, in internal z units (exaggeration and the mercator
         * stretch applied), sampled from the grid a cached TEXTURE was built from.
         *
         * This is tangram's model: it samples the elevation raster the tile renders with
         * (`ElevationManager::getElevation` -> `elevationLerp(*raster.texture, ...)`), so there is
         * one representation and a CPU query cannot disagree with what is drawn. Ours has been the
         * odd one out - ElevationManager's grid LRU is independent of this cache, so a grid is
         * routinely evicted while the texture built from it goes on rendering, and a CACHED_ONLY
         * query answers "no data" for ground that is plainly on screen. Measured at the Millau
         * camera: 0 hits in 3900 queries, with the terrain drawn correctly throughout.
         *
         * The grid is already held by the entry (CacheEntry::grid), so this needs no extra
         * retention - only somewhere to ask.
         *
         * Sampled at the level the RENDERER draws `zoom` with - the same getDetailDataTile mapping
         * getTexture resolves through - not at whatever level happens to be the most detailed one
         * cached. Picking the most detailed made the answer depend on load order: the same bridge
         * portal measured 441 m and 663 m within one run, so two halves of one deck were baked at
         * different heights.
         *
         * @return False when the renderer has no elevation for the tile holding the point.
         */
        bool getDisplayHeight(double internalX, double internalY, int zoom, double& height) const;

        /**
         * Resolves every tile at the elevation source's own maximum detail instead of at the level
         * the terrain mesh can express. For a cache feeding per-fragment shading (the terrain
         * paint): the mesh cap costs two zoom levels of relief, which at high zoom is all of it.
         */
        /**
         * The grid tiles that entered the cache since the last call, and clears the list. A CPU
         * height query is answered from these entries, so a chord or a building base resolved
         * while only an ancestor was cached must be resolved again once the tile's own level lands
         * - on this terrain the two differ by 50-70 m, and the stale copy is drawn beside the
         * fresh one. ElevationManager's own data version cannot stand in: the grid loads, and the
         * texture is encoded some frames later.
         *
         * Reported per TILE rather than as a counter so the renderer re-resolves only what stands
         * over them, which is mapbox's model - a global bump re-did every building on screen each
         * time any DEM tile landed.
         */
        std::vector<MapTile> drainContentChanges();

        void setDetailLevels(int extraLevels);

        void clear();

    private:
        class BorderBitmap; // a Bitmap whose border strips can be rewritten in place

        // Grids are identified by their TILE, not by the pointer they happen to live behind: the
        // elevation cache is an LRU, so the same DEM tile can be decoded into a new object at any
        // time. Comparing pointers made that re-decode look like new data and re-encoded (or, since
        // border patching, re-patched) a texture whose content had not changed at all.
        using GridKey = long long; // grid tile id, or -1 for a missing neighbour
        static GridKey gridKey(const std::shared_ptr<ElevationTileGrid>& grid);

        // How good the border on one side is, and the ONLY reason to touch a texture that is
        // already up: 0 = this grid's own duplicated edge texels, 1 = a coarser ancestor sampled
        // geographically, 2 = the exact same-level neighbour. DEM data never changes, so a border
        // is only ever worth redoing when a side can be filled BETTER than it was. Comparing the
        // neighbour set instead re-patched whenever a neighbour was merely evicted from the grid
        // LRU - during a pan that was ~70 patches a second, and a patch is four glTexSubImage2D
        // calls into a live texture, measured at ~1 ms each on the render thread.
        using BorderQuality = std::array<int, 8>;
        static constexpr BorderQuality NO_BORDERS = { { 0, 0, 0, 0, 0, 0, 0, 0 } };

        struct CacheEntry {
            std::shared_ptr<ElevationTileGrid> grid;
            GridKey gridKeyValue = -1;
            BorderQuality borderQuality = NO_BORDERS;
            // The grids each side's border was taken from, kept so that a later patch can REUSE
            // them: the elevation grid LRU drops and re-decodes tiles all the time, and rebuilding
            // the ring from whatever happens to be cached right now would let a side that already
            // had its exact neighbour fall back to an ancestor - and then improve again, which is
            // an endless patch loop. Holding them makes the quality per side monotone, so the ring
            // converges and stops.
            std::array<std::shared_ptr<ElevationTileGrid>, 8> neighbours;
            std::shared_ptr<BorderBitmap> bitmap; // what the texture is rebuilt from after a context loss
            std::shared_ptr<Texture> texture;
            // The node texture beside it: the grid's node field (ElevationTileGrid::encodeNodeTexture),
            // which the vertex stage displaces from. Same lifetime, same patching.
            std::shared_ptr<BorderBitmap> nodeBitmap;
            std::shared_ptr<Texture> nodeTexture;
            std::uint64_t lastUsed = 0; // LRU stamp
        };

        // What the worker is asked for, and what it hands back.
        struct EncodeJob {
            long long gridTileId = -1;
            std::shared_ptr<ElevationTileGrid> grid;
            std::array<std::shared_ptr<ElevationTileGrid>, 8> neighbours;
            BorderQuality borderQuality = NO_BORDERS;
            bool bordersOnly = false; // the entry already has this grid's texture; only its ring changed
        };
        // The BITMAP, not the encoded bytes: building it copies the whole padded texture
        // (514x514 RGBA, a megabyte, byte by byte in Bitmap::loadFromUncompressedBytes) and that
        // copy has no reason to be on the render thread - measured on the Crosscall, north pan,
        // it was 20% of it, with another 11% freeing the encode buffer there.
        struct EncodedTexture {
            long long gridTileId = -1;
            GridKey gridKeyValue = -1;
            BorderQuality borderQuality = NO_BORDERS;
            std::shared_ptr<ElevationTileGrid> grid;
            std::array<std::shared_ptr<ElevationTileGrid>, 8> neighbours;
            std::shared_ptr<BorderBitmap> bitmap;
            std::shared_ptr<BorderBitmap> nodeBitmap;
        };

        // A neighbour arriving changes ONLY the 2-texel ring of the texture (the border itself,
        // and this grid's outermost row/column where a coarser neighbour box-filters it). During a
        // pan that is the common case by far, and re-encoding a megabyte for it is most of what
        // this pipeline costs. The ring is encoded on the worker and patched into the existing
        // texture and its bitmap - same result, ~1.5% of the texels.
        struct BorderPatch {
            long long gridTileId = -1;
            GridKey gridKeyValue = -1;      // the patch is void if the entry's grid changed meanwhile
            BorderQuality borderQuality = NO_BORDERS;
            std::shared_ptr<ElevationTileGrid> grid;
            std::array<std::shared_ptr<ElevationTileGrid>, 8> neighbours;
            ElevationTileGrid::BorderStrips strips;
            ElevationTileGrid::BorderStrips nodeStrips; // the node texture's edge rows/columns
        };

        // The texture carries the source raster's texels (3 bytes for an RGB DEM), so the cap is
        // what decides whether extra DEM detail is affordable - each level beyond the mesh cap
        // needs four times the textures.
        static constexpr std::size_t MAX_CACHED_TEXTURES = 128;
        // Uploads per frame, and the time they may take. A tile with no texture yet renders FLAT,
        // so a budget that is too tight is visible as terrain that stays flat while it catches up;
        // one that is too loose brings back the stall this pipeline exists to remove. Time-bounded
        // with a floor of one upload, so progress is guaranteed however slow the device is.
        static constexpr int MAX_UPLOADS_PER_FRAME = 8;
        static constexpr double MAX_UPLOAD_MS_PER_FRAME = 6.0;
        static constexpr std::size_t MAX_ENCODE_QUEUE = 32;

        bool resolveEntry(const vt::TileId& tileId, MapTile& gridTileOut);
        static void fillTexture(const CacheEntry& entry, float metersToInternal, vt::GLTileRenderer::TerrainTexture& terrainTexture);
        // Queues an encode unless the same grid+neighbours is already queued, encoding or ready.
        // 'bordersOnly' when the entry already holds a texture built from this exact grid and only
        // the neighbours changed.
        void requestEncode(long long gridTileId, const std::shared_ptr<ElevationTileGrid>& grid, const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, const BorderQuality& borderQuality, bool bordersOnly);
        void uploadReadyTextures();
        void applyBorderPatches();
        void runEncodeWorker();
        void stopEncodeWorker();
        void evictLeastRecentlyUsed();

        const std::shared_ptr<ElevationManager> _elevationManager;
        const std::shared_ptr<GLResourceManager> _glResourceManager;
        std::map<long long, CacheEntry> _cache; // keyed by the grid tile id
        std::map<long long, MapTile> _frameResolved; // render tile id -> its elevation grid tile (zoom -1: no data), reset every frame
        std::vector<MapTile> _contentChanges; // grid tiles that landed, drained by the renderer
        int _detailLevels = 0; // elevation levels resolved BEYOND what the mesh can express
        std::uint64_t _accessCounter = 0; // monotonic LRU clock
        std::uint64_t _frameStartCounter = 0; // LRU clock at the start of the current frame

        // Encode pipeline. The worker only ever touches the queues and the grids handed to it.
        mutable std::mutex _encodeMutex;
        std::condition_variable _encodeCondition;
        std::deque<EncodeJob> _encodeQueue;      // drained newest first: the newest request is the visible one
        std::set<long long> _encodePending;      // queued or being encoded
        std::deque<EncodedTexture> _encodedQueue; // waiting for the GL thread to upload
        std::deque<BorderPatch> _patchQueue;      // waiting for the GL thread to patch
        std::vector<std::uint8_t> _encodeScratch; // worker-thread only: the encode buffer, reused
        std::vector<std::uint8_t> _nodeScratch;   // worker-thread only: the node texture buffer
        std::unique_ptr<std::thread> _encodeThread;
        bool _encodeStopped = false;
    };
}

#endif

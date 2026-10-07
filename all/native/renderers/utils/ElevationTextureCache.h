/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ELEVATIONTEXTURECACHE_H_
#define _MASSIF_ELEVATIONTEXTURECACHE_H_

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "core/MapTile.h"
#include "terrain/ElevationTileGrid.h" // BorderStrips is a member of a queued patch
#include "terrain/TerrainSkirts.h"

#include <vt/GLTileRenderer.h>

namespace massif {
    class Bitmap;
    class ElevationManager;
    class ElevationTileGrid;
    class GLResourceManager;
    class HalfFloatTexture;
    class Texture;

    /**
     * vt::GLTileRenderer terrain texture provider over the ElevationManager grid cache, one texture per DEM
     * tile shared by all layers. Encoded on a worker and uploaded under a per-frame budget, never inside the
     * frame that first samples it. GL thread only (the worker touches nothing else). Internal class.
     */
    class ElevationTextureCache {
    public:
        ElevationTextureCache(const std::shared_ptr<ElevationManager>& elevationManager, const std::shared_ptr<GLResourceManager>& glResourceManager);
        ~ElevationTextureCache();

        const std::shared_ptr<ElevationManager>& getElevationManager() const { return _elevationManager; }

        /**
         * Fills the terrain texture for the tile from the best cached grid (possibly an ancestor's).
         * Returns false while no texture is ready: the encode is queued and the tile renders flat meanwhile.
         */
        bool getTexture(const vt::TileId& tileId, vt::GLTileRenderer::TerrainTexture& terrainTexture);

        /** The node heights the tile is displaced by, from the same entry getTexture picks: the uploaded bytes, patches included. */
        bool getDrawnNodeField(const vt::TileId& tileId, NodeFieldView& field);
        /** Changes whenever an upload or a border patch changes what some tile is drawn with. */
        unsigned int getDrawnVersion() const { return _drawnVersion; }
        /** While held every tile renders flat, as before any elevation landed (MapRenderer's first rise). */
        void setHeld(bool held) { _held = held; }

        /**
         * Uploads what the worker encoded (within budget) and drops the per-frame tile resolution memo,
         * which saves every render pass 9 locked lookups per tile.
         */
        void beginFrame();

        /**
         * Called on the encode worker, outside every lock, when an encode finishes: a still map must
         * request a frame or the relief is never uploaded.
         */
        void setTextureReadyListener(const std::function<void()>& listener);

        // Baked queries: one level covers the frame between decode and texture; beyond is a smoothed average.
        static const int BASE_MAX_ANCESTOR_LEVELS = 1;
        // Labels: zoomGap 3 plus one frame of texture lag; deeper entries belong to a previous camera.
        static const int LABEL_MAX_ANCESTOR_LEVELS = 4;

        /**
         * Ground height in internal z (exaggeration, Mercator stretch) from the grid a cached texture was built from, at
         * the level the renderer draws `zoom` with, so CPU queries match what is drawn (tangram's model).
         * maxAncestorLevels bounds how coarse the answer may be; prefetch false requests nothing missing.
         * @return False when the renderer has no elevation for the tile holding the point.
         */
        bool getDisplayHeight(double internalX, double internalY, int zoom, bool smooth, double& height, int maxAncestorLevels = BASE_MAX_ANCESTOR_LEVELS, bool prefetch = true) const;

        /**
         * Tiles whose texture content changed as of this frame's start, per tile so only what stands over
         * them is re-resolved (mapbox's model). Not drained: every sharing tile layer invalidates from it.
         */
        const std::vector<MapTile>& getFrameContentChanges() const;

        void setDetailLevels(int extraLevels);

        /**
         * Asks for at least this many extra detail levels; beginFrame applies the max over the sharing
         * layers, since a per-layer set would clear the cache on each change.
         */
        void requestDetailLevels(int extraLevels);
        /**
         * Shader tap distance in ground metres (TerrainOptions::getNormalSampleDistance); borders widen to
         * keep taps on real neighbour data. 0 keeps the 1-texel border. A change re-encodes everything.
         */
        void setBorderMetres(float metres);

        void clear();

    private:
        class BorderBitmap; // a Bitmap whose border strips can be rewritten in place

        // By tile, not pointer: the elevation LRU re-decodes the same DEM tile into new objects.
        using GridKey = long long; // ElevationTileGrid::getSerial, or -1 for a missing neighbour
        static GridKey gridKey(const std::shared_ptr<ElevationTileGrid>& grid);

        // Per side, the only reason to re-patch an uploaded texture (neighbour sets churn with evictions):
        // 0 = own duplicated edge texels, 1 = a coarser ancestor, 2 = the exact same-level neighbour.
        using BorderQuality = std::array<int, 8>;
        static constexpr BorderQuality NO_BORDERS = { { 0, 0, 0, 0, 0, 0, 0, 0 } };

        struct CacheEntry {
            std::shared_ptr<ElevationTileGrid> grid;
            GridKey gridKeyValue = -1;
            BorderQuality borderQuality = NO_BORDERS;
            int border = 1; // texels of neighbour data around the raster (getTextureBorderTexels)
            // Held so a later patch reuses them and border quality stays monotone despite LRU churn.
            std::array<std::shared_ptr<ElevationTileGrid>, 8> neighbours;
            std::shared_ptr<BorderBitmap> bitmap; // what the texture is rebuilt from after a context loss
            std::shared_ptr<Texture> texture;
            // ElevationTileGrid::encodeNodeTexture, which the vertex stage displaces from.
            std::shared_ptr<BorderBitmap> nodeBitmap;
            std::shared_ptr<Texture> nodeTexture;
            std::shared_ptr<HalfFloatTexture> gradientTexture; // ElevationGradient, the lit surfaces' normals
            std::uint64_t lastUsed = 0; // LRU stamp
            std::chrono::steady_clock::time_point lastUsedTime; // the frame that last drew it: held, see evictLeastRecentlyUsed
        };

        struct EncodeJob {
            long long gridTileId = -1;
            std::shared_ptr<ElevationTileGrid> grid;
            std::array<std::shared_ptr<ElevationTileGrid>, 8> neighbours;
            BorderQuality borderQuality = NO_BORDERS;
            bool bordersOnly = false; // the entry already has this grid's texture; only its ring changed
            int border = 1;
        };
        // The bitmap, not the encoded bytes: building it is a full copy that belongs off the render thread.
        struct EncodedTexture {
            long long gridTileId = -1;
            GridKey gridKeyValue = -1;
            BorderQuality borderQuality = NO_BORDERS;
            std::shared_ptr<ElevationTileGrid> grid;
            std::array<std::shared_ptr<ElevationTileGrid>, 8> neighbours;
            std::shared_ptr<BorderBitmap> bitmap;
            std::shared_ptr<BorderBitmap> nodeBitmap;
            std::vector<std::uint16_t> gradient;
            int border = 1;
        };

        // A neighbour arriving changes only the (border + 1)-texel ring, patched in place (~1.5% of the texels).
        struct BorderPatch {
            long long gridTileId = -1;
            GridKey gridKeyValue = -1;      // the patch is void if the entry's grid changed meanwhile
            BorderQuality borderQuality = NO_BORDERS;
            std::shared_ptr<ElevationTileGrid> grid;
            std::array<std::shared_ptr<ElevationTileGrid>, 8> neighbours;
            ElevationTileGrid::BorderStrips strips;
            ElevationTileGrid::BorderStrips nodeStrips; // the node texture's edge rows/columns
            int border = 1; // void unless the entry was encoded with the same one
        };

        // Each detail level beyond the mesh cap needs four times the textures.
        static constexpr std::size_t MAX_CACHED_TEXTURES = 128;
        // Time-bounded with a floor of one upload, so progress is guaranteed on any device.
        static constexpr int MAX_UPLOADS_PER_FRAME = 8;
        static constexpr double MAX_UPLOAD_MS_PER_FRAME = 6.0;
        static constexpr std::size_t MAX_ENCODE_QUEUE = 32;

        bool resolveEntry(const vt::TileId& tileId, MapTile& gridTileOut);
        static void fillTexture(const CacheEntry& entry, float metersToInternal, vt::GLTileRenderer::TerrainTexture& terrainTexture);
        // No-op if the same grid+neighbours is already queued, encoding or ready.
        void requestEncode(long long gridTileId, const std::shared_ptr<ElevationTileGrid>& grid, const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, const BorderQuality& borderQuality, bool bordersOnly, int border);
        void uploadReadyTextures();
        void applyBorderPatches();
        void runEncodeWorker();
        void stopEncodeWorker();
        bool evictLeastRecentlyUsed(); // false when everything was used lately
        CacheEntry* findDrawnEntry(const vt::TileId& tileId);

        const std::shared_ptr<ElevationManager> _elevationManager;
        const std::shared_ptr<GLResourceManager> _glResourceManager;
        std::map<long long, CacheEntry> _cache; // keyed by the grid tile id
        std::map<long long, MapTile> _frameResolved; // render tile id -> its elevation grid tile (zoom -1: no data), reset every frame
        unsigned int _drawnVersion = 0;
        bool _held = false;
        std::vector<MapTile> _contentChanges; // grid tiles that landed, drained by the renderer

        // Metres; a building's base is read at this posting so parts of one building agree.
        static constexpr double SMOOTH_BASE_POSTING = 50.0;
        // Where the search for that posting starts; it walks coarser from here, never finer.
        static const int SMOOTH_BASE_ZOOM_HINT = 12;

        int _detailLevels = 0; // elevation levels resolved BEYOND what the mesh can express
        float _borderMetres = 0.0f; // see setBorderMetres
        std::uint64_t _accessCounter = 0; // monotonic LRU clock
        std::chrono::steady_clock::time_point _frameTime; // when the current frame began

        std::vector<MapTile> _frameContentChanges; // see getFrameContentChanges
        int _requestedDetailLevels = 0; // see requestDetailLevels, reset every frame
        std::function<void()> _textureReadyListener; // set once, before the worker starts

        // The worker only ever touches the queues and the grids handed to it.
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

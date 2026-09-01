#include "ElevationTextureCache.h"
#include "core/MapBounds.h"
#include "core/MapTile.h"
#include "graphics/Bitmap.h"
#include "renderers/utils/GLResourceManager.h"
#include "renderers/utils/Texture.h"
#include "terrain/ElevationManager.h"
#include "terrain/ElevationTileGrid.h"
#include "utils/Const.h"
#include "utils/Log.h"

#include <vt/RenderStats.h>

#include <algorithm>
#include <chrono>
#include <vector>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace massif {

    // A Bitmap whose border strips can be rewritten after construction. The bitmap is what the
    // texture is rebuilt from when the GL context is lost, so a border patch that only reached the
    // GPU would be silently undone; patching both keeps them in step. Bitmap owns the pixel data
    // and Texture holds the bitmap alive anyway, so this costs no extra memory.
    class ElevationTextureCache::BorderBitmap : public Bitmap {
    public:
        BorderBitmap(const unsigned char* pixelData, unsigned int width, unsigned int height, ColorFormat::ColorFormat colorFormat, int bytesPerRow) :
            Bitmap(pixelData, width, height, colorFormat, bytesPerRow)
        {
        }

        // Writes a sub-rectangle in the bitmap's own (bottom-up) row order.
        void writeRect(int x, int y, int width, int height, const std::vector<std::uint8_t>& data) {
            std::size_t bpp = _bytesPerPixel;
            if (x < 0 || y < 0 || x + width > static_cast<int>(_width) || y + height > static_cast<int>(_height)) {
                return;
            }
            if (data.size() < static_cast<std::size_t>(width) * height * bpp) {
                return;
            }
            for (int row = 0; row < height; row++) {
                std::size_t dst = ((static_cast<std::size_t>(y) + row) * _width + x) * bpp;
                std::size_t src = static_cast<std::size_t>(row) * width * bpp;
                std::copy(data.begin() + src, data.begin() + src + static_cast<std::size_t>(width) * bpp, _pixelData.begin() + dst);
            }
        }
    };

#ifdef __ANDROID__
    // Patch a texture's border ring instead of re-encoding it whole when a neighbour lands.
    // Off with: adb shell setprop debug.massif.demborderpatch 0
    static bool isBorderPatchEnabled() {
        static const bool enabled = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return !(__system_property_get("debug.massif.demborderpatch", property) > 0 && property[0] == '0');
        }();
        return enabled;
    }
#else
    static bool isBorderPatchEnabled() {
        return true;
    }
#endif

    ElevationTextureCache::GridKey ElevationTextureCache::gridKey(const std::shared_ptr<ElevationTileGrid>& grid) {
        return grid ? grid->getTile().getTileId() : -1;
    }

    ElevationTextureCache::ElevationTextureCache(const std::shared_ptr<ElevationManager>& elevationManager, const std::shared_ptr<GLResourceManager>& glResourceManager) :
        _elevationManager(elevationManager),
        _glResourceManager(glResourceManager),
        _cache()
    {
    }

    bool ElevationTextureCache::getTexture(const vt::TileId& tileId, vt::GLTileRenderer::TerrainTexture& terrainTexture) {
        int tileMask = (1 << tileId.zoom) - 1;
        MapTile mapTile(tileId.x & tileMask, std::min(std::max(tileId.y, 0), tileMask), tileId.zoom, 0);
        long long mapTileId = mapTile.getTileId();

        // The provider is called once per tile per render pass; resolve the tile at most
        // once per frame and reuse the resolution for the remaining passes.
        MapTile gridTile;
        bool resolved = false;
        auto frameIt = _frameResolved.find(mapTileId);
        if (frameIt != _frameResolved.end()) {
            gridTile = frameIt->second;
            resolved = gridTile.getZoom() >= 0;
        } else {
            // Nothing cached for this tile: fall back to the nearest ANCESTOR that does resolve.
            // Zooming out asks for coarse DEM tiles that were never fetched (the finer ones cannot
            // stand in - the walk only ever goes up), so a tile is routinely left with no elevation
            // for a moment. Rendering it FLAT there is what makes its roads snap to straight lines
            // over ground that IS displaced, until its own grid arrives and they jump onto it. An
            // ancestor is coarser but geometrically correct, and it is the same height field the
            // shared ground stands on meanwhile, so the two agree.
            vt::TileId resolveTileId = tileId;
            for (;;) {
                resolved = resolveEntry(resolveTileId, gridTile);
                if (resolved || resolveTileId.zoom <= 0) {
                    break;
                }
                resolveTileId = resolveTileId.getParent();
            }
            _frameResolved[mapTileId] = (resolved ? gridTile : MapTile(0, 0, -1, 0));
        }
        if (!resolved) {
            return false;
        }

        // The exact grid's texture if it is on the GPU, otherwise the nearest ancestor's: a tile
        // whose own texture is still being encoded must not be left WITHOUT elevation. Its surface
        // would render flat, and - since the drape bake re-fills a tile from scratch - a terrain
        // paint would bake that tile with no hillshade at all, taking the shading off ground that
        // already had it. An ancestor texture is coarser and geometrically correct (the uv mapping
        // covers it), which is the same stand-in the drape itself uses while a tile catches up.
        for (MapTile tile = gridTile; ; tile = tile.getParent()) {
            auto it = _cache.find(tile.getTileId());
            if (it != _cache.end() && it->second.texture && it->second.texture->getTexId() != 0) {
                it->second.lastUsed = ++_accessCounter;
                fillTexture(it->second, static_cast<float>(_elevationManager->getExaggeration() * Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE), terrainTexture);
                return true;
            }
            if (tile.getZoom() <= 0) {
                return false;
            }
        }
    }

    bool ElevationTextureCache::resolveEntry(const vt::TileId& tileId, MapTile& gridTileOut) {
        // Resolve the best already-decoded elevation grid for the tile. This mirrors the
        // CPU displacement path (TerrainTileTransformer), so the GPU-sampled heights stay
        // consistent with element placement, hit testing and label anchors.
        int tileMask = (1 << tileId.zoom) - 1;
        MapTile mapTile(tileId.x & tileMask, std::min(std::max(tileId.y, 0), tileMask), tileId.zoom, 0);

        // The tile that carries this tile's elevation data: its own level, capped by the data
        // source maximum zoom and by the resolution the surface mesh can express (see
        // ElevationManager::setSurfaceResolution). The cap lives in the manager so that the
        // displaced surface and every CPU-side elevation query use the same height field.
        MapTile dataTile = _elevationManager->getDetailDataTile(mapTile, _detailLevels);

        std::shared_ptr<ElevationTileGrid> grid = _elevationManager->getDataTileGrid(dataTile, ElevationManager::LoadMode::CACHED_ONLY);
        if (!grid || !(grid->getTile() == dataTile)) {
            // Missing or resolved through a coarser ancestor: request the real thing, ahead of
            // any neighbour request. Until it arrives this tile is displaced by an ancestor grid
            // - which is a 2x2 average of its children, i.e. a different height field than the
            // neighbouring tiles that already have their own level - and the surface tears along
            // the shared edge. Loading the right level fast is what closes it.
            // No-op when elevation prefetching is disabled or the tile is already queued.
            _elevationManager->prefetchTileGrid(dataTile, 2);
        }
        if (!grid || grid->getWidth() < 1 || grid->getHeight() < 1) {
            return false;
        }

        // Fetch the neighbour grids: the texture gets a 1-texel border taken from them, so
        // adjacent tiles bilinearly interpolate across tile borders from identical texel
        // pairs - same-level DEM tile borders become seam-free. With seamless tile edges
        // enabled, coarser ancestor grids are accepted as well and sampled geographically
        // (ElevationTileGrid::encodeTextureWithBorders): not perfectly symmetric across a
        // level change, but real DEM data instead of a duplicated edge texel.
        bool seamless = _elevationManager->isSeamlessTileEdgesEnabled();
        const MapTile& gridTile = grid->getTile();
        int gridMask = (1 << gridTile.getZoom()) - 1;
        BorderQuality qualities = NO_BORDERS;
        int slot = 0;
        auto neighbourGrid = [&, this](int dx, int dy) -> std::shared_ptr<ElevationTileGrid> {
            int index = slot++;
            int ny = gridTile.getY() + dy; // XYZ convention: y grows south, grid rows grow north
            if (ny < 0 || ny > gridMask) {
                return std::shared_ptr<ElevationTileGrid>();
            }
            MapTile neighbourTile((gridTile.getX() + dx) & gridMask, ny, gridTile.getZoom(), 0);
            std::shared_ptr<ElevationTileGrid> neighbour = _elevationManager->getDataTileGrid(neighbourTile, ElevationManager::LoadMode::CACHED_ONLY);
            if (!neighbour || !(neighbour->getTile() == neighbourTile)) {
                // Border texels want the real neighbour tile, but after every tile's own level:
                // a missing neighbour costs one texel of border accuracy, a missing own level
                // displaces the whole tile. Diagonal neighbours only fill the corner texel where
                // four tiles meet, so they come last.
                _elevationManager->prefetchTileGrid(neighbourTile, dx == 0 || dy == 0 ? 1 : 0);
            }
            if (neighbour && !(neighbour->getTile() == neighbourTile) && !seamless) {
                neighbour.reset(); // strict mode: only exact same-level neighbours
            }
            if (neighbour && neighbour->getTile() == gridTile) {
                neighbour.reset(); // our own grid covers the neighbour: the texture is already continuous there
            }
            qualities[index] = (!neighbour ? 0 : (neighbour->getTile() == neighbourTile ? 2 : 1));
            return neighbour;
        };
        // order: W, E, S, N, SW, SE, NW, NE ('south' = smaller internal y = larger XYZ tile y)
        std::array<std::shared_ptr<ElevationTileGrid>, 8> neighbours = { {
            neighbourGrid(-1, 0), neighbourGrid(1, 0), neighbourGrid(0, 1), neighbourGrid(0, -1),
            neighbourGrid(-1, 1), neighbourGrid(1, 1), neighbourGrid(-1, -1), neighbourGrid(1, -1)
        } };

#if MASSIF_VT_RENDER_STATS
        vt::RenderStats::demTileZoomGap.store(mapTile.getZoom() - gridTile.getZoom());
#endif
        gridTileOut = gridTile;
        auto it = _cache.find(gridTile.getTileId());
        GridKey key = gridKey(grid);
        bool gridChanged = (it == _cache.end() || it->second.gridKeyValue != key);
        if (!gridChanged) {
            // Keep every side that is already at least as good as what is cached right now, from
            // the grid the entry took it from - see CacheEntry::neighbours.
            for (std::size_t i = 0; i < qualities.size(); i++) {
                if (qualities[i] <= it->second.borderQuality[i]) {
                    qualities[i] = it->second.borderQuality[i];
                    neighbours[i] = it->second.neighbours[i];
                }
            }
        }
        // A border is only redone when a side can now be filled BETTER (see BorderQuality): the
        // DEM behind it does not change, so a neighbour that was merely evicted is no reason to
        // touch a texture that is already correct.
        bool bordersImproved = false;
        if (!gridChanged) {
            for (std::size_t i = 0; i < qualities.size(); i++) {
                bordersImproved = bordersImproved || qualities[i] > it->second.borderQuality[i];
            }
        }
        if (gridChanged || bordersImproved) {
            // Not encoded yet, or encoded from data that has since changed (the tile's own grid
            // arrived, or a neighbour did and the border can be filled properly now). Either way
            // the work goes to the worker; what is already on the GPU keeps being used until the
            // new texture is uploaded, so a border refinement never blanks the tile.
            // Only the ring depends on the neighbours, so when this grid's own texture is already
            // on the GPU a neighbour landing is a patch, not a rebuild.
            bool bordersOnly = isBorderPatchEnabled() && !gridChanged && it->second.bitmap && it->second.texture;
            requestEncode(gridTile.getTileId(), grid, neighbours, qualities, bordersOnly);
            if (it == _cache.end()) {
                return false;
            }
            // The entry keeps serving its current texture meanwhile. Its neighbours are recorded
            // only once the patch is actually applied: the encode queue drops its oldest jobs when
            // it overflows, and an entry that already claimed the new neighbours would never ask
            // again - a dropped patch would silently leave a stale border. Re-requesting every
            // frame until it lands is a failed insert into _encodePending, which is the cheap side.
        }
        it->second.lastUsed = ++_accessCounter;
        return it->second.texture && it->second.texture->getTexId() != 0;
    }

    void ElevationTextureCache::requestEncode(long long gridTileId, const std::shared_ptr<ElevationTileGrid>& grid, const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, const BorderQuality& borderQuality, bool bordersOnly) {
        std::lock_guard<std::mutex> lock(_encodeMutex);
        if (_encodeStopped) {
            return;
        }
        if (!_encodePending.insert(gridTileId).second) {
            return; // queued or being encoded; the newest inputs win when it is re-requested later
        }
        // Newest first (the queue is drained from the back): the newest request belongs to the
        // current viewport, while the oldest may already have scrolled away.
        _encodeQueue.push_back(EncodeJob { gridTileId, grid, neighbours, borderQuality, bordersOnly });
        while (_encodeQueue.size() > MAX_ENCODE_QUEUE) {
            _encodePending.erase(_encodeQueue.front().gridTileId);
            _encodeQueue.pop_front();
        }
        if (!_encodeThread) {
            _encodeThread = std::make_unique<std::thread>([this]() { runEncodeWorker(); });
        }
        _encodeCondition.notify_one();
    }

    void ElevationTextureCache::runEncodeWorker() {
        while (true) {
            EncodeJob job;
            {
                std::unique_lock<std::mutex> lock(_encodeMutex);
                _encodeCondition.wait(lock, [this]() { return _encodeStopped || !_encodeQueue.empty(); });
                if (_encodeStopped) {
                    return;
                }
                job = std::move(_encodeQueue.back());
                _encodeQueue.pop_back();
            }

            if (job.bordersOnly) {
                // Only the ring: ~1.5% of the texels of a full encode, and no megabyte to copy
                // into a Bitmap afterwards.
                BorderPatch patch;
                patch.gridTileId = job.gridTileId;
                patch.gridKeyValue = gridKey(job.grid);
                patch.borderQuality = job.borderQuality;
                patch.neighbours = job.neighbours;
                patch.grid = job.grid;
                VT_STAT_CLOCK(patchClock);
                job.grid->encodeTextureBorders(job.neighbours, patch.strips);
                VT_STAT_SPLIT(demEncodeNs, patchClock);
                VT_STAT_INC(demBorderPatches);

                std::lock_guard<std::mutex> lock(_encodeMutex);
                _encodePending.erase(job.gridTileId);
                if (_encodeStopped) {
                    return;
                }
                for (auto it = _patchQueue.begin(); it != _patchQueue.end(); it++) {
                    if (it->gridTileId == patch.gridTileId) {
                        _patchQueue.erase(it); // an older ring for the same tile is superseded
                        break;
                    }
                }
                _patchQueue.push_back(std::move(patch));
                continue;
            }

            EncodedTexture encoded;
            encoded.gridTileId = job.gridTileId;
            encoded.gridKeyValue = gridKey(job.grid);
            encoded.borderQuality = job.borderQuality;
            encoded.neighbours = job.neighbours;
            encoded.grid = job.grid;
            int width = job.grid->getWidth() + 2;
            int height = job.grid->getHeight() + 2;
            // The scratch buffer belongs to this thread alone and is reused by every job, so the
            // megabyte behind it is allocated once instead of per encode.
            VT_STAT_CLOCK(encodeClock);
            job.grid->encodeTextureWithBorders(job.neighbours, _encodeScratch);
            // The encoded rows are south-to-north, i.e. already bottom-up in the Bitmap
            // convention. Bitmap treats a POSITIVE stride as top-down input and flips the
            // rows - pass a negative stride so the data is taken as-is (a flipped texture
            // mirrors every tile's terrain north-south).
            // The texture is the SOURCE raster's own format and texels - tangram's model, where
            // the elevation raster is bound as it arrived and decoded in the shader. Nothing is
            // requantised, so the height field keeps the data source's own precision (1/256m for
            // terrarium, 0.1m for mapbox). The negative stride keeps the rows as copied.
            int texelBytes = job.grid->getBytesPerTexel();
            encoded.bitmap = std::make_shared<BorderBitmap>(_encodeScratch.data(), width, height, job.grid->getColorFormat(), -texelBytes * width);
            VT_STAT_SPLIT(demEncodeNs, encodeClock);
            VT_STAT_INC(demEncodes);

            {
                std::lock_guard<std::mutex> lock(_encodeMutex);
                _encodePending.erase(job.gridTileId);
                if (_encodeStopped) {
                    return;
                }
                // Supersede an older encode of the same grid that has not been uploaded yet: only
                // the newest inputs matter, and uploading both would cost two uploads for one tile.
                for (auto it = _encodedQueue.begin(); it != _encodedQueue.end(); it++) {
                    if (it->gridTileId == encoded.gridTileId) {
                        _encodedQueue.erase(it);
                        break;
                    }
                }
                _encodedQueue.push_back(std::move(encoded));
            }
        }
    }

    void ElevationTextureCache::uploadReadyTextures() {
        auto uploadStart = std::chrono::steady_clock::now();
        for (int i = 0; i < MAX_UPLOADS_PER_FRAME; i++) {
            if (i > 0 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart).count() > MAX_UPLOAD_MS_PER_FRAME) {
                return; // the rest waits for the next frame; those tiles keep their old texture
            }
            EncodedTexture encoded;
            {
                std::lock_guard<std::mutex> lock(_encodeMutex);
                if (_encodedQueue.empty()) {
                    return;
                }
                encoded = std::move(_encodedQueue.back()); // newest first, as in the encode queue
                _encodedQueue.pop_back();
            }

            auto it = _cache.find(encoded.gridTileId);
            if (it == _cache.end() && _cache.size() >= MAX_CACHED_TEXTURES) {
                evictLeastRecentlyUsed();
            }
            CacheEntry entry;
            entry.grid = encoded.grid;
            entry.gridKeyValue = encoded.gridKeyValue;
            entry.borderQuality = encoded.borderQuality;
            entry.neighbours = encoded.neighbours;
            entry.lastUsed = (it != _cache.end() ? it->second.lastUsed : _accessCounter);
            entry.bitmap = encoded.bitmap;
            VT_STAT_CLOCK(uploadClock);
            entry.texture = _glResourceManager->create<Texture>(encoded.bitmap, false, false); // no mipmaps, clamp to edge
            VT_STAT_SPLIT(demUploadNs, uploadClock);
            VT_STAT_INC(demUploads);
            _cache.insert_or_assign(encoded.gridTileId, std::move(entry));
        }
    }

    void ElevationTextureCache::applyBorderPatches() {
        // Cheap enough not to need the upload budget: four glTexSubImage2D calls over a 2-texel
        // ring, against a full 514x514 upload for the same visual result.
        std::deque<BorderPatch> patches;
        {
            std::lock_guard<std::mutex> lock(_encodeMutex);
            patches.swap(_patchQueue);
        }
        for (BorderPatch& patch : patches) {
            auto it = _cache.find(patch.gridTileId);
            if (it == _cache.end() || it->second.gridKeyValue != patch.gridKeyValue || !it->second.bitmap || !it->second.texture) {
                continue; // the entry was rebuilt or evicted meanwhile; the patch is void
            }
            int width = patch.grid->getWidth() + 2;
            int height = patch.grid->getHeight() + 2;
            const std::shared_ptr<BorderBitmap>& bitmap = it->second.bitmap;
            const std::shared_ptr<Texture>& texture = it->second.texture;
            VT_STAT_CLOCK(patchUploadClock);
            // Both the CPU copy and the GPU texture: the bitmap is what the texture is rebuilt
            // from after a context loss.
            bitmap->writeRect(0, 0, width, 2, patch.strips.south);
            texture->updateSubImage(0, 0, width, 2, patch.strips.south.data());
            bitmap->writeRect(0, height - 2, width, 2, patch.strips.north);
            texture->updateSubImage(0, height - 2, width, 2, patch.strips.north.data());
            bitmap->writeRect(0, 0, 2, height, patch.strips.west);
            texture->updateSubImage(0, 0, 2, height, patch.strips.west.data());
            bitmap->writeRect(width - 2, 0, 2, height, patch.strips.east);
            texture->updateSubImage(width - 2, 0, 2, height, patch.strips.east.data());
            VT_STAT_SPLIT(demPatchNs, patchUploadClock);
            VT_STAT_INC(demPatchUploads);
            it->second.borderQuality = patch.borderQuality;
            it->second.neighbours = patch.neighbours;
        }
    }

    void ElevationTextureCache::evictLeastRecentlyUsed() {
        // Evict the least-recently-used entry, NOT the whole cache. A full flush re-encodes and
        // re-uploads every texture whenever the working set exceeds the cap, which stalls the
        // render thread on fast multi-level zooms (the working set of visible + neighbour DEM
        // tiles briefly exceeds the cap). LRU keeps the warm set. Entries already used in this
        // frame are kept if possible: dropping one would make the tile fall back to flat in its
        // remaining render passes.
        auto lru = _cache.end();
        for (auto entryIt = _cache.begin(); entryIt != _cache.end(); entryIt++) {
            if (entryIt->second.lastUsed > _frameStartCounter) {
                continue;
            }
            if (lru == _cache.end() || entryIt->second.lastUsed < lru->second.lastUsed) {
                lru = entryIt;
            }
        }
        if (lru == _cache.end()) {
            lru = std::min_element(_cache.begin(), _cache.end(), [](const std::pair<const long long, CacheEntry>& a, const std::pair<const long long, CacheEntry>& b) {
                return a.second.lastUsed < b.second.lastUsed;
            });
        }
        if (lru != _cache.end()) {
            _cache.erase(lru);
        }
    }

    void ElevationTextureCache::fillTexture(const CacheEntry& entry, float metersToInternal, vt::GLTileRenderer::TerrainTexture& terrainTexture) {
        // The texture covers the grid bounds extended by the 1-texel border
        const MapBounds& bounds = entry.grid->getInternalBounds();
        double texelX = (bounds.getMax().getX() - bounds.getMin().getX()) / entry.grid->getWidth();
        double texelY = (bounds.getMax().getY() - bounds.getMin().getY()) / entry.grid->getHeight();
        terrainTexture.textureId = entry.texture->getTexId();
        terrainTexture.textureSize = cglib::vec2<int>(entry.grid->getWidth() + 2, entry.grid->getHeight() + 2);
        terrainTexture.internalOrigin = cglib::vec2<double>(bounds.getMin().getX() - texelX, bounds.getMin().getY() - texelY);
        terrainTexture.internalSize = cglib::vec2<double>(bounds.getMax().getX() - bounds.getMin().getX() + 2 * texelX, bounds.getMax().getY() - bounds.getMin().getY() + 2 * texelY);
        std::array<float, 4> decode = entry.grid->getDecode();
        terrainTexture.decode = cglib::vec4<float>(decode[0], decode[1], decode[2], decode[3]);
        terrainTexture.decodeOffset = entry.grid->getDecodeOffset();
        terrainTexture.metersToInternal = metersToInternal;
        terrainTexture.mercatorYScale = static_cast<float>(2.0 * Const::PI / Const::WORLD_SIZE);
        // What the DEM itself resolves, for consumers that shade from it (the terrain paint):
        // the ground distance one texel covers at the equator.
        terrainTexture.metersPerTexel = static_cast<float>(texelX * Const::EARTH_CIRCUMFERENCE / Const::WORLD_SIZE);
    }

    ElevationTextureCache::~ElevationTextureCache() {
        stopEncodeWorker();
    }

    void ElevationTextureCache::stopEncodeWorker() {
        std::unique_ptr<std::thread> thread;
        {
            std::lock_guard<std::mutex> lock(_encodeMutex);
            _encodeStopped = true;
            _encodeQueue.clear();
            _encodePending.clear();
            _encodedQueue.clear();
            _patchQueue.clear();
            thread = std::move(_encodeThread);
        }
        _encodeCondition.notify_all();
        if (thread && thread->joinable()) {
            thread->join();
        }
    }

    void ElevationTextureCache::setDetailLevels(int extraLevels) {
        if (_detailLevels != extraLevels) {
            _detailLevels = extraLevels;
            clear(); // every entry was resolved at the other level
        }
    }

    bool ElevationTextureCache::getDisplayHeight(double internalX, double internalY, double& height) const {
        MapPos pos(internalX, internalY, 0);
        // The grid the last query landed in, first: a caller sampling a bridge's two portals, or a
        // label's every vertex, walks the same tile over and over, and the scan below is linear in
        // the cache.
        std::shared_ptr<ElevationTileGrid> grid = _lastSampledGrid;
        if (!grid || !grid->getInternalBounds().contains(pos)) {
            grid.reset();
            // Most DETAILED entry covering the point. Several cover it - an ancestor stands in for
            // a tile whose own level has not arrived - and the smallest is the closest to what the
            // ground is actually drawn from.
            double bestSize = 0;
            for (auto it = _cache.begin(); it != _cache.end(); it++) {
                const std::shared_ptr<ElevationTileGrid>& candidate = it->second.grid;
                if (!candidate || !candidate->getInternalBounds().contains(pos)) {
                    continue;
                }
                const MapBounds& bounds = candidate->getInternalBounds();
                double size = bounds.getMax().getX() - bounds.getMin().getX();
                if (!grid || size < bestSize) {
                    grid = candidate;
                    bestSize = size;
                }
            }
            if (!grid) {
                return false;
            }
            _lastSampledGrid = grid;
        }
        double meters = grid->sampleHeight(internalX, internalY);
        height = meters * _elevationManager->getExaggeration() * _elevationManager->getDisplayScale(internalY);
        return true;
    }

    void ElevationTextureCache::beginFrame() {
        // Textures encoded since the last frame go up now, ahead of the draws that sample them,
        // and border refinements are patched into the ones already there.
        uploadReadyTextures();
        applyBorderPatches();
#if MASSIF_VT_RENDER_STATS
        vt::RenderStats::demTexturesLive.store(static_cast<long long>(_cache.size()));
        vt::RenderStats::demTexturesResolved.store(static_cast<long long>(_frameResolved.size()));
#endif
        _frameResolved.clear();
        _frameStartCounter = _accessCounter;
    }

    void ElevationTextureCache::clear() {
        _cache.clear();
        _frameResolved.clear();
        {
            std::lock_guard<std::mutex> lock(_encodeMutex);
            _encodeQueue.clear();
            _encodePending.clear();
            _encodedQueue.clear(); // encoded from grids this cache no longer stands behind
            _patchQueue.clear();
        }
    }
}

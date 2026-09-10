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

    // A Bitmap whose border strips can be rewritten after construction: the bitmap is what the
    // texture is rebuilt from on GL context loss, so a border patch that only reached the GPU would
    // be silently undone. Texture holds the bitmap alive anyway, so this costs no extra memory.
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
            // Nothing cached for this tile: fall back to the nearest ANCESTOR that resolves. Flat
            // would snap its roads to straight lines over ground that IS displaced; an ancestor is
            // coarser but geometrically correct, and the shared ground stands on it meanwhile.
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
        // whose own texture is still being encoded must not be left WITHOUT elevation, or its surface
        // renders flat and a terrain paint bakes it with no hillshade at all.
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

        // The tile that carries this tile's elevation data: its own level, capped by the source max
        // zoom and by what the surface mesh can express. The cap lives in the manager so the
        // displaced surface and every CPU-side query use the same height field.
        MapTile dataTile = _elevationManager->getDetailDataTile(mapTile, _detailLevels);

        std::shared_ptr<ElevationTileGrid> grid = _elevationManager->getDataTileGrid(dataTile, ElevationManager::LoadMode::CACHED_ONLY);
        if (!grid || !(grid->getTile() == dataTile)) {
            // Missing or resolved through a coarser ancestor: request the real thing ahead of any
            // neighbour. Until it arrives this tile is displaced by a different height field than
            // its neighbours and the surface tears along the shared edge. No-op when queued.
            _elevationManager->prefetchTileGrid(dataTile, 2);
        }
        if (!grid || grid->getWidth() < 1 || grid->getHeight() < 1) {
            return false;
        }

        // Fetch the neighbour grids: the texture takes a 1-texel border from them, so adjacent
        // tiles interpolate across the seam from identical texel pairs. With seamless edges, coarser
        // ancestors are accepted too - real DEM data instead of a duplicated edge texel.
        bool seamless = _elevationManager->isSeamlessTileEdgesEnabled();
        const MapTile& gridTile = grid->getTile();
        // The far ground of a tilted view, whose border is worth a fraction of a pixel: its fetch
        // is not worth delaying the ground under the camera.
        bool farFromView = static_cast<float>(gridTile.getZoom()) < _viewZoom - NEIGHBOUR_PREFETCH_MAX_LEVELS_BELOW_VIEW;
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
            if ((!neighbour || !(neighbour->getTile() == neighbourTile)) && !farFromView) {
                // Border texels want the real neighbour, but after every tile's own level: a missing
                // neighbour costs one texel of accuracy, a missing own level displaces the whole
                // tile. Diagonals only fill the corner texel, so they come last.
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
            // Not encoded yet, or encoded from data that has since changed. What is on the GPU keeps
            // being used until the new texture is uploaded, so a border refinement never blanks the
            // tile - and only the ring depends on neighbours, so a neighbour landing is a patch.
            bool bordersOnly = isBorderPatchEnabled() && !gridChanged && it->second.bitmap && it->second.texture;
            requestEncode(gridTile.getTileId(), grid, neighbours, qualities, bordersOnly);
            if (it == _cache.end()) {
                return false;
            }
            // Neighbours are recorded only once the patch is applied: the encode queue drops its
            // oldest jobs on overflow, and an entry that already claimed them would never ask again.
            // Re-requesting every frame is a failed insert into _encodePending, which is cheap.
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
                job.grid->encodeNodeTextureBorders(job.neighbours, patch.nodeStrips);
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
            // The encoded rows are south-to-north, already bottom-up in the Bitmap convention, and
            // Bitmap flips a POSITIVE stride - so pass a negative one and take the data as-is.

            // The texture keeps the SOURCE raster's format and texels (tangram's model), so nothing
            // is requantised and the height field keeps the data source's own precision.
            int texelBytes = job.grid->getBytesPerTexel();
            encoded.bitmap = std::make_shared<BorderBitmap>(_encodeScratch.data(), width, height, job.grid->getColorFormat(), -texelBytes * width);
            // The node texture: (nodes + 1)^2 in the same encoding, rows south-to-north as well.
            if (job.grid->getNodesPerEdge() > 0) {
                int nodeSize = job.grid->getNodesPerEdge() + 1;
                job.grid->encodeNodeTexture(job.neighbours, _nodeScratch);
                encoded.nodeBitmap = std::make_shared<BorderBitmap>(_nodeScratch.data(), nodeSize, nodeSize, job.grid->getColorFormat(), -texelBytes * nodeSize);
            }
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
            if (encoded.nodeBitmap) {
                entry.nodeBitmap = encoded.nodeBitmap;
                entry.nodeTexture = _glResourceManager->create<Texture>(encoded.nodeBitmap, false, false);
            }
            VT_STAT_SPLIT(demUploadNs, uploadClock);
            VT_STAT_INC(demUploads);
            _cache.insert_or_assign(encoded.gridTileId, std::move(entry));
            if (encoded.grid) { _contentChanges.push_back(encoded.grid->getTile()); }
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
            // The node texture's four edge rows/columns, the only node texels a neighbour changes.
            if (it->second.nodeBitmap && it->second.nodeTexture && !patch.nodeStrips.south.empty()) {
                int nodeSize = patch.grid->getNodesPerEdge() + 1;
                const std::shared_ptr<BorderBitmap>& nodeBitmap = it->second.nodeBitmap;
                const std::shared_ptr<Texture>& nodeTexture = it->second.nodeTexture;
                nodeBitmap->writeRect(0, 0, nodeSize, 1, patch.nodeStrips.south);
                nodeTexture->updateSubImage(0, 0, nodeSize, 1, patch.nodeStrips.south.data());
                nodeBitmap->writeRect(0, nodeSize - 1, nodeSize, 1, patch.nodeStrips.north);
                nodeTexture->updateSubImage(0, nodeSize - 1, nodeSize, 1, patch.nodeStrips.north.data());
                nodeBitmap->writeRect(0, 0, 1, nodeSize, patch.nodeStrips.west);
                nodeTexture->updateSubImage(0, 0, 1, nodeSize, patch.nodeStrips.west.data());
                nodeBitmap->writeRect(nodeSize - 1, 0, 1, nodeSize, patch.nodeStrips.east);
                nodeTexture->updateSubImage(nodeSize - 1, 0, 1, nodeSize, patch.nodeStrips.east.data());
            }
            VT_STAT_SPLIT(demPatchNs, patchUploadClock);
            VT_STAT_INC(demPatchUploads);
            it->second.borderQuality = patch.borderQuality;
            it->second.neighbours = patch.neighbours;
        }
    }

    void ElevationTextureCache::evictLeastRecentlyUsed() {
        // Evict the least-recently-used entry, NOT the whole cache: a full flush re-encodes and
        // re-uploads everything whenever the working set exceeds the cap, stalling the render thread
        // on fast zooms. Entries already used this frame are kept, or their tile falls back to flat.
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
        // The node texture: texel i is node i, on the cell corner at i / nodes of the grid, so
        // uv 0 sits half a node before the west/south edge and uv 1 half a node past the other.
        int nodes = entry.grid->getNodesPerEdge();
        if (entry.nodeTexture && entry.nodeTexture->getTexId() != 0 && nodes > 0) {
            double nodeX = (bounds.getMax().getX() - bounds.getMin().getX()) / nodes;
            double nodeY = (bounds.getMax().getY() - bounds.getMin().getY()) / nodes;
            terrainTexture.nodeTextureId = entry.nodeTexture->getTexId();
            terrainTexture.nodeTextureSize = cglib::vec2<int>(nodes + 1, nodes + 1);
            terrainTexture.nodeOrigin = cglib::vec2<double>(bounds.getMin().getX() - 0.5 * nodeX, bounds.getMin().getY() - 0.5 * nodeY);
            terrainTexture.nodeSize = cglib::vec2<double>((nodes + 1) * nodeX, (nodes + 1) * nodeY);
        } else {
            terrainTexture.nodeTextureId = 0;
        }
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

    bool ElevationTextureCache::getDisplayHeight(double internalX, double internalY, int zoom, bool smooth, double& height) const {
        if (zoom < 0) {
            return false;
        }
        // The tile HOLDING the point, which is not the tile being drawn: a span reaches past its
        // own tile by design, and its far portal belongs to a neighbour.
        int extent = 1 << zoom;
        double u = internalX / Const::WORLD_SIZE + 0.5;
        double v = 0.5 - internalY / Const::WORLD_SIZE;
        int x = static_cast<int>(std::floor(u * extent));
        int y = static_cast<int>(std::floor(v * extent));
        if (x < 0 || y < 0 || x >= extent || y >= extent) {
            return false;
        }
        double displayScale = _elevationManager->getExaggeration() * _elevationManager->getDisplayScale(internalY);
        if (smooth) {
            // A building's base: the DEM smoothed to SMOOTH_BASE_POSTING metres, not the lidar level
            // the surface is drawn from, or every piece of a building stands at its own height. The
            // LEVEL comes from the grid's own resolution, never fixed - the tile size changes it.
            double metersPerInternal = 1.0 / std::max(1.0e-12, _elevationManager->getDisplayScale(internalY));
            int shift = std::max(0, zoom - SMOOTH_BASE_ZOOM_HINT);
            std::shared_ptr<ElevationTileGrid> grid;
            for (int pass = 0; pass < 2; pass++) {
                MapTile coarse = _elevationManager->getDetailDataTile(MapTile(x >> shift, y >> shift, zoom - shift, 0), _detailLevels);
                // An ancestor answering is fine - coarser is smoother - and may be all there ever is:
                // the grid cache resolves a level through an ancestor and then never fetches it, so
                // insisting on the exact level left every building on the sentinel for good.
                grid = _elevationManager->getDataTileGrid(coarse, ElevationManager::LoadMode::CACHED_ONLY);
                if (!grid) {
                    _elevationManager->prefetchTileGrid(coarse, 2);
                    return false;
                }
                double posting = Const::WORLD_SIZE / (1 << grid->getTile().getZoom()) / std::max(1, grid->getWidth()) * metersPerInternal;
                int coarser = (posting > 0 ? static_cast<int>(std::ceil(std::log(SMOOTH_BASE_POSTING / posting) / std::log(2.0))) : 0);
                if (coarser <= 0 || shift + coarser > zoom) {
                    break;
                }
                shift += coarser;
            }
            height = grid->sampleHeight(internalX, internalY) * displayScale;
            return true;
        }
        // The same mapping getTexture resolves a tile through, so the query lands on the height field
        // the surface is drawn from. Deliberately NOT _frameResolved, which is only filled as tiles
        // are drawn. The grid comes from the entry the TEXTURE holds - the manager's LRU drops it.
        vt::TileId tileId(zoom, x, y);
        int dataZoom = -1;
        for (;;) {
            MapTile dataTile = _elevationManager->getDetailDataTile(MapTile(tileId.x, tileId.y, tileId.zoom, 0), _detailLevels);
            if (dataZoom < 0) {
                dataZoom = dataTile.getZoom();
            }
            // A base is BAKED into the vertices, so a far ancestor is not a coarser answer but a
            // wrong one: over Paris a footprint that fell through came back at 127 m where the DEM
            // says 34. The vertex keeps its sentinel and resolves for real once the tile lands.
            if (dataZoom - dataTile.getZoom() > BASE_MAX_ANCESTOR_LEVELS) {
                return false;
            }
            auto cacheIt = _cache.find(dataTile.getTileId());
            if (cacheIt != _cache.end() && cacheIt->second.grid) {
                double meters = cacheIt->second.grid->sampleNodeHeight(internalX, internalY); // the surface, as a chord must sit on it
                height = meters * displayScale;
                return true;
            }
            // Only a DRAWN tile has a texture, and a bridge's far portal is routinely in a tile
            // that is not: the same grid from the manager's LRU is the same height field, and
            // when neither has it the tile is asked for, or the portal off screen never resolved.
            std::shared_ptr<ElevationTileGrid> grid = _elevationManager->getDataTileGrid(dataTile, ElevationManager::LoadMode::CACHED_ONLY);
            if (grid && grid->getTile() == dataTile) {
                height = grid->sampleNodeHeight(internalX, internalY) * displayScale;
                return true;
            }
            if (dataTile.getZoom() == dataZoom) {
                _elevationManager->prefetchTileGrid(dataTile, 2);
            }
            if (tileId.zoom <= 0) {
                return false;
            }
            tileId = tileId.getParent();
        }
    }

    void ElevationTextureCache::beginFrame(float viewZoom) {
        _viewZoom = viewZoom;
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

    std::vector<MapTile> ElevationTextureCache::drainContentChanges() {
        std::vector<MapTile> changes;
        changes.swap(_contentChanges);
        return changes;
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

#include "TerrainDrapeCache.h"
#include "renderers/utils/GLContext.h"
#include "terrain/DrapeStandIn.h"

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include <algorithm>

namespace massif {

    // Same ceiling Texture uses; drivers clamp to their own maximum anyway.
    const int TerrainDrapeCache::MAX_ANISOTROPY = 8;
    const std::size_t TerrainDrapeCache::MAX_POOLED_TEXTURES = 32;
    // Keep a generation of tiles past the visible cover: a zoom or pan walks back over the same
    // tiles and re-acquiring means re-baking every layer of each. A BYTE budget, not a tile count -
    // 160 entries are 10 MB at 128 and 640 MB at 1024. docs/internals/rendering/04-terrain.md.
const std::size_t TerrainDrapeCache::MAX_BYTES = 96 * 1024 * 1024;
const std::size_t TerrainDrapeCache::MIN_ENTRIES = 24;
const std::size_t TerrainDrapeCache::MAX_ENTRIES = 160;

    bool TerrainDrapeCache::Key::operator < (const Key& other) const {
        if (stack != other.stack) {
            return stack < other.stack;
        }
        if (tileId.zoom != other.tileId.zoom) {
            return tileId.zoom < other.tileId.zoom;
        }
        if (tileId.x != other.tileId.x) {
            return tileId.x < other.tileId.x;
        }
        return tileId.y < other.tileId.y;
    }

    TerrainDrapeCache::TerrainDrapeCache() :
        _resolution(1024),
        _stackSignature(0),
        _frameBuffer(0),
        _entries(),
        _texturePool(),
        _frameCounter(0)
    {
    }

    TerrainDrapeCache::~TerrainDrapeCache() {
        // GL resources must be released explicitly via deleteResources() while the context is
        // current; the destructor may run after it is gone.
    }

    int TerrainDrapeCache::getResolution() const {
        return _resolution;
    }

    void TerrainDrapeCache::setResolution(int resolution) {
        int value = std::min(2048, std::max(128, resolution));
        if (value == _resolution) {
            return;
        }
        _resolution = value;
        // Every cached texture is the old size, so none of them can be reused.
        for (auto it = _entries.begin(); it != _entries.end(); it++) {
            GLuint texture = it->second.texture;
            glDeleteTextures(1, &texture);
        }
        _entries.clear();
        for (unsigned int texture : _texturePool) {
            GLuint tex = texture;
            glDeleteTextures(1, &tex);
        }
        _texturePool.clear();
        for (unsigned int texture : _maskTexturePool) {
            GLuint tex = texture;
            glDeleteTextures(1, &tex);
        }
        _maskTexturePool.clear();
    }

    void TerrainDrapeCache::setStackSignature(std::size_t signature) {
        if (signature == _stackSignature) {
            return;
        }
        _stackSignature = signature;
        // The textures are kept: a stale picture of the same ground is a better thing to show for
        // the two or three frames the re-bake takes than the flat fill dropping them would leave.
        // They just stop being trusted - re-baked with the blank-tile budget, and never copied
        // into another tile.
        for (auto it = _entries.begin(); it != _entries.end(); it++) {
            it->second.stale = it->second.baked || it->second.seeded;
            // A seed is a copy of other tiles' pictures, so a seed made from the old stack is old
            // content too, and unlike a bake it has no fingerprint to notice that with.
            it->second.seeded = false;
        }
    }

    bool TerrainDrapeCache::isStale(const vt::TileId& tileId, int stack) const {
        auto it = _entries.find(Key { tileId, stack });
        return it != _entries.end() && it->second.stale;
    }

    void TerrainDrapeCache::beginFrame() {
        _frameCounter++;
        for (auto it = _entries.begin(); it != _entries.end(); it++) {
            it->second.used = false;
        }
    }

    // Measurement switch for the mipmapped drape textures: debug.massif.drapemip 0 goes back to
    // GL_LINEAR with no mipmap chain. Read once (Android only).
#ifdef __ANDROID__
    bool TerrainDrapeCache::isMipmapEnabled() {
        static const bool enabled = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return !(__system_property_get("debug.massif.drapemip", property) > 0 && property[0] == '0');
        }();
        return enabled;
    }
#else
    bool TerrainDrapeCache::isMipmapEnabled() {
        return true;
    }
#endif

    unsigned int TerrainDrapeCache::createTexture(bool mask) {
        std::vector<unsigned int>& pool = (mask ? _maskTexturePool : _texturePool);
        if (!pool.empty()) {
            unsigned int texture = pool.back();
            pool.pop_back();
            return texture;
        }
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        // A coverage mask is one channel: R8 (core in ES3, which both platforms require - see
        // CLAUDE.md) rather than a quarter-used RGBA, so a mask costs a quarter of a drape.
        if (mask) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, _resolution, _resolution, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _resolution, _resolution, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Mipmapped, because a drape texture is almost always MINIFIED: the bake resolution is
        // sized for the widest a tile can ever get on screen (see TileRenderer::
        // resolveDrapeResolution), so an ordinary tile samples a texture several times larger than
        // its footprint. With GL_LINEAR that is four texels from an incoherent footprint per
        // fragment - a texture cache miss per fragment, and minification aliasing on top.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, isMipmapEnabled() ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        // ANISOTROPIC, because the drape is looked at along the ground. At tilt the sampler's two
        // axes are wildly different and mip selection follows the WORST of them, so everything
        // baked into the drape smears along the view direction - by an amount that changes with
        // the camera's rotation. It is why a contact shadow stops matching its footprint, and it
        // blurs draped roads and labels in exactly the same way. Only with mipmaps: it selects
        // between levels, so there is nothing to select from without them.
        if (isMipmapEnabled() && GLContext::TEXTURE_FILTER_ANISOTROPIC) {
            GLint deviceMaxAnisotropy = 0;
            glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &deviceMaxAnisotropy);
            if (deviceMaxAnisotropy > 1) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(MAX_ANISOTROPY, deviceMaxAnisotropy));
            }
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return texture;
    }

    unsigned int TerrainDrapeCache::acquire(const vt::TileId& tileId, int stack, std::size_t fingerprint, bool& needsBake, bool& hasContent) {
        Key key { tileId, stack };
        Entry& entry = _entries[key];
        if (entry.texture == 0) {
            entry.texture = createTexture(stack > 0);
            entry.bytes = static_cast<std::size_t>(_resolution) * _resolution * (stack > 0 ? 1 : 4);
            entry.baked = false;
            entry.seeded = false;
            entry.stale = false;
        }
        entry.used = true;
        entry.lastUsedFrame = _frameCounter;
        // A changed fingerprint means the layers covering this tile changed - a style layer
        // finished loading, or a proxy was replaced by its native tile - so the texture is stale.
        // Stale means baked from a stack that no longer exists. The fingerprint does not always
        // catch that: dropping a layer leaves the remaining layers' content - and their hashes -
        // unchanged for tiles the dropped layer had nothing in.
        needsBake = !entry.baked || entry.stale || entry.fingerprint != fingerprint;
        // A seeded texture is not a bake, but it does show this tile's ground - sampling it is
        // right, and it is the difference between a stand-in and a flat fill.
        hasContent = entry.baked || entry.seeded;
        return entry.texture;
    }

    void TerrainDrapeCache::generateMipmaps(unsigned int texture) {
        // After every write to level 0 - a bake, or a blit that seeds a tile from another one -
        // or the smaller levels still hold the previous picture.
        if (texture == 0 || !isMipmapEnabled()) {
            return;
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void TerrainDrapeCache::markBaked(const vt::TileId& tileId, int stack, std::size_t fingerprint, std::size_t layerMask) {
        auto it = _entries.find(Key { tileId, stack });
        if (it != _entries.end()) {
            it->second.fingerprint = fingerprint;
            it->second.layerMask = layerMask;
            it->second.baked = true;
            it->second.seeded = false;
            it->second.stale = false;
        }
    }

    void TerrainDrapeCache::markSeeded(const vt::TileId& tileId, int stack) {
        auto it = _entries.find(Key { tileId, stack });
        if (it != _entries.end()) {
            it->second.seeded = true;
        }
    }

    bool TerrainDrapeCache::isBaked(const vt::TileId& tileId, int stack) const {
        auto it = _entries.find(Key { tileId, stack });
        return it != _entries.end() && it->second.baked;
    }

    std::size_t TerrainDrapeCache::bakedLayerMask(const vt::TileId& tileId, int stack) const {
        auto it = _entries.find(Key { tileId, stack });
        if (it == _entries.end() || !it->second.baked) {
            return 0;
        }
        return it->second.layerMask;
    }

    unsigned int TerrainDrapeCache::findBaked(const vt::TileId& tileId, int stack) {
        auto it = _entries.find(Key { tileId, stack });
        // A stale entry must never be a source: seeding or standing in with it copies the previous
        // stack's picture into tiles that never had it, and a seed carries no fingerprint, so the
        // old content then survives every check that would have replaced it.
        if (it == _entries.end() || !it->second.baked || it->second.stale) {
            return 0;
        }
        // Standing in for a tile that has no texture of its own IS use: without this the entry
        // looks idle to the eviction pass below while it is the only thing on screen.
        it->second.used = true;
        it->second.lastUsedFrame = _frameCounter;
        return it->second.texture;
    }

    std::vector<std::pair<vt::TileId, unsigned int>> TerrainDrapeCache::findBakedDescendants(const vt::TileId& tileId, int stack) {
        // _entries is ordered by (zoom, x, y), so the candidates come out coarsest first, which is
        // what coarsestCover expects.
        std::vector<vt::TileId> candidates;
        std::vector<Entry*> entries;
        for (auto it = _entries.begin(); it != _entries.end(); it++) {
            if (it->first.stack != stack || !it->second.baked || it->second.stale) {
                continue; // same rule as findBaked: a stale entry is the previous stack's picture
            }
            candidates.push_back(it->first.tileId);
            entries.push_back(&it->second);
        }

        std::vector<std::pair<vt::TileId, unsigned int>> result;
        for (std::size_t index : DrapeStandIn::coarsestCover(tileId, candidates)) {
            entries[index]->used = true;
            entries[index]->lastUsedFrame = _frameCounter;
            result.emplace_back(candidates[index], entries[index]->texture);
        }
        return result;
    }

    unsigned int TerrainDrapeCache::getFrameBuffer() {
        if (_frameBuffer == 0) {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            _frameBuffer = fbo;
        }
        return _frameBuffer;
    }

    // Measurement switch: debug.massif.drapebudget 0 caps the cache by tile COUNT again, as it did
    // before the budget existed. Read once (Android only).
#ifdef __ANDROID__
    bool TerrainDrapeCache::isBudgetEnabled() {
        static const bool enabled = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return !(__system_property_get("debug.massif.drapebudget", property) > 0 && property[0] == '0');
        }();
        return enabled;
    }
#else
    bool TerrainDrapeCache::isBudgetEnabled() {
        return true;
    }
#endif

#ifdef __ANDROID__
    bool TerrainDrapeCache::isCoverageMaskEnabled() {
        static const bool enabled = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return !(__system_property_get("debug.massif.drapemask", property) > 0 && property[0] == '0');
        }();
        return enabled;
    }
#else
    bool TerrainDrapeCache::isCoverageMaskEnabled() {
        return true;
    }
#endif

    std::size_t TerrainDrapeCache::maxEntries() const {
        if (!isBudgetEnabled()) {
            return MAX_ENTRIES;
        }
        std::size_t bytesPerEntry = static_cast<std::size_t>(_resolution) * _resolution * 4;
        if (bytesPerEntry == 0) {
            return MAX_ENTRIES;
        }
        std::size_t entries = MAX_BYTES / bytesPerEntry;
        return std::min(MAX_ENTRIES, std::max(MIN_ENTRIES, entries));
    }

    std::size_t TerrainDrapeCache::cachedBytes() const {
        std::size_t bytes = 0;
        for (auto it = _entries.begin(); it != _entries.end(); it++) {
            bytes += it->second.bytes;
        }
        return bytes;
    }

    void TerrainDrapeCache::endFrame() {
        // BYTES, not a count: a coverage mask entry (#175) is R8 and costs a quarter of a colour
        // drape, so a count would let the masks eat a quarter of the cache's tiles for nothing.
        // MIN_ENTRIES stays the floor, in colour-drape equivalents.
        std::size_t maxCount = maxEntries();
        std::size_t colourBytes = static_cast<std::size_t>(_resolution) * _resolution * 4;
        std::size_t maxBytes = (isBudgetEnabled() ? std::max(MAX_BYTES, MIN_ENTRIES * colourBytes) : MAX_ENTRIES * colourBytes);
        std::size_t bytes = cachedBytes();
        if (_entries.size() <= maxCount && bytes <= maxBytes) {
            return; // keep unused tiles cached; they come back constantly while panning/zooming
        }
        // Over budget: evict the least recently used entries, never one used this frame.
        std::vector<std::pair<unsigned int, Key> > candidates;
        candidates.reserve(_entries.size());
        for (auto it = _entries.begin(); it != _entries.end(); it++) {
            if (!it->second.used) {
                candidates.emplace_back(it->second.lastUsedFrame, it->first);
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const std::pair<unsigned int, Key>& a, const std::pair<unsigned int, Key>& b) {
            return a.first < b.first;
        });
        for (std::size_t i = 0; i < candidates.size(); i++) {
            if (_entries.size() <= maxCount && bytes <= maxBytes) {
                break;
            }
            auto it = _entries.find(candidates[i].second);
            if (it == _entries.end()) {
                continue;
            }
            std::vector<unsigned int>& pool = (candidates[i].second.stack > 0 ? _maskTexturePool : _texturePool);
            if (pool.size() < MAX_POOLED_TEXTURES) {
                pool.push_back(it->second.texture);
            } else {
                GLuint texture = it->second.texture;
                glDeleteTextures(1, &texture);
            }
            bytes -= std::min(bytes, it->second.bytes);
            _entries.erase(it);
        }
    }

    void TerrainDrapeCache::deleteResources() {
        for (auto it = _entries.begin(); it != _entries.end(); it++) {
            GLuint texture = it->second.texture;
            glDeleteTextures(1, &texture);
        }
        _entries.clear();
        for (unsigned int texture : _texturePool) {
            GLuint tex = texture;
            glDeleteTextures(1, &tex);
        }
        _texturePool.clear();
        for (unsigned int texture : _maskTexturePool) {
            GLuint tex = texture;
            glDeleteTextures(1, &tex);
        }
        _maskTexturePool.clear();
        if (_frameBuffer != 0) {
            GLuint fbo = _frameBuffer;
            glDeleteFramebuffers(1, &fbo);
            _frameBuffer = 0;
        }
    }

}

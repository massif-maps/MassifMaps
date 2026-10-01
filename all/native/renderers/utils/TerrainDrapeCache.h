/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINDRAPECACHE_H_
#define _MASSIF_TERRAINDRAPECACHE_H_

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

#include <vt/TileId.h>

namespace massif {

    /**
     * Shared render-to-texture drape target for 3D terrain: every drapeable tile layer bakes into one
     * per-tile texture, sharing one surface draw and depth domain. Keyed by (tile, stack): stack 0 is the
     * RGBA drape, 1..K the R8 coverage masks that occlude live no-drape layers (#175). GL thread only.
     */
    class TerrainDrapeCache {
    public:
        TerrainDrapeCache();
        ~TerrainDrapeCache();

        int getResolution() const;
        /**
         * Sets the per-tile texture resolution; existing textures are dropped.
         */
        void setResolution(int resolution);

        /**
         * Identifies the layer stack the textures are baked from (fingerprints miss a replaced layer). A change
         * marks every entry stale: still drawable, re-baked first, and never a seed or stand-in, so an old
         * style cannot flash back or spread.
         */
        void setStackSignature(std::size_t signature);
        /**
         * Whether this tile's texture was baked from an earlier layer stack.
         */
        bool isStale(const vt::TileId& tileId, int stack) const;

        /**
         * Starts a frame. Tiles not acquired before endFrame() are released back to the pool.
         */
        void beginFrame();
        /**
         * Returns the texture for a tile, creating or recycling one. needsBake: fingerprint mismatch, clear
         * and bake. hasContent: safe to sample; a recycled texture shows another tile until baked, so a
         * caller skipping the bake must not draw it.
         */
        unsigned int acquire(const vt::TileId& tileId, int stack, std::size_t fingerprint, bool& needsBake, bool& hasContent);
        /**
         * The same at a size of the caller's (0: getResolution()). A texture of the wrong size (DrapeTuning::needsResize)
         * is swapped for a new one; the old one comes back in `replaced`, for the caller to copy its picture across and
         * then hand to recycle(), or it is recycled here when `replaced` is null.
         */
        unsigned int acquire(const vt::TileId& tileId, int stack, std::size_t fingerprint, int resolution, unsigned int* replaced, bool& needsBake, bool& hasContent);
        /**
         * The size a cached texture was made at, 0 if none.
         */
        int getTextureResolution(const vt::TileId& tileId, int stack) const;
        /**
         * Returns a texture from acquire's `replaced` to the pool of its size.
         */
        void recycle(unsigned int texture, bool mask, int resolution);
        // Default budget for setMaxBytes; public because the automatic bake resolution must agree with it.
        static const std::size_t MAX_BYTES;
        /**
         * Overrides the byte budget (TerrainOptions::DrapeCacheSize). 0 restores MAX_BYTES.
         */
        void setMaxBytes(std::size_t maxBytes);
        // debug.massif.drapebudget 0 restores the tile-count cap and uncapped resolution. Android demo builds only.
        static bool isBudgetEnabled();
        // debug.massif.drapemask 0 turns the no-drape occlusion masks off (#175). Android demo builds only.
        static bool isCoverageMaskEnabled();

        /**
         * Rebuilds the mipmap chain of a drape texture; must follow every write to its level 0.
         */
        static void generateMipmaps(unsigned int texture);
        static bool isMipmapEnabled();

        /**
         * Records an actual bake (not on acquire, which would poison entries never baked).
         * layerMask is the set of drape layers that put something in the texture.
         */
        void markBaked(const vt::TileId& tileId, int stack, std::size_t fingerprint, std::size_t layerMask);
        /**
         * The layers the cached texture was baked from, or 0 if never baked. A tile missing a layer
         * deserves an earlier re-bake.
         */
        std::size_t bakedLayerMask(const vt::TileId& tileId, int stack) const;
        /**
         * Records a texture filled from other cached tiles rather than baked: safe to sample, still needs a
         * bake, and never a seed source itself, or the picture degrades with every copy.
         */
        void markSeeded(const vt::TileId& tileId, int stack);
        /**
         * Whether the texture holds a real bake, as opposed to nothing or a seed.
         */
        bool isBaked(const vt::TileId& tileId, int stack) const;
        /**
         * Returns the texture of an already-baked tile, or 0, for a stand-in until a bake lands. Marks the
         * entry used this frame, or it is evicted at the end of the frame it stood in on.
         */
        unsigned int findBaked(const vt::TileId& tileId, int stack);
        /**
         * The coarsest baked tiles inside this one at any depth (one pass over the cache), covering the ground
         * once: the stand-in for a zoomed-out tile until its bake lands. Marks them used, as findBaked does.
         */
        std::vector<std::pair<vt::TileId, unsigned int>> findBakedDescendants(const vt::TileId& tileId, int stack);
        /**
         * Returns the framebuffer to bake into, creating it on first use.
         */
        unsigned int getFrameBuffer();
        /**
         * Releases textures not acquired during this frame.
         */
        void endFrame();

        /**
         * Deletes all GL resources. Must be called on the GL thread while the context is alive.
         */
        void deleteResources();

    private:
        struct Key {
            vt::TileId tileId;
            int stack;

            bool operator < (const Key& other) const;
        };

        struct Entry {
            unsigned int texture = 0;
            int resolution = 0;
            std::size_t bytes = 0; // 4 bytes/texel for a colour drape, 1 for an R8 coverage mask
            std::size_t fingerprint = 0;
            std::size_t layerMask = 0;
            bool baked = false;
            bool seeded = false;
            bool stale = false; // baked from an earlier layer stack
            bool used = false;
            unsigned int lastUsedFrame = 0;
        };

        // mask: a one-channel R8 coverage mask (stack > 0) rather than the RGBA colour drape.
        unsigned int createTexture(bool mask, int resolution);
        // Back to the pool of its size, or deleted past MAX_POOLED_TEXTURES.
        void pool(unsigned int texture, bool mask, int resolution);
        std::size_t cachedBytes() const;

        static const int MAX_ANISOTROPY;
        static const std::size_t MAX_POOLED_TEXTURES; // recycled textures kept between frames
        static const std::size_t MAX_ENTRIES;         // cached tiles kept alive across frames (upper bound)
        static const std::size_t MIN_ENTRIES;         // ... but never fewer than this, whatever the resolution costs
        std::size_t maxEntries() const;

        std::size_t _maxBytes;
        int _resolution;
        std::size_t _stackSignature;
        unsigned int _frameBuffer;
        std::map<Key, Entry> _entries;
        // By size: a pooled texture keeps its format and its dimensions. R8 masks apart.
        std::map<int, std::vector<unsigned int>> _texturePools;
        std::map<int, std::vector<unsigned int>> _maskTexturePools;
        unsigned int _frameCounter;
    };

}

#endif

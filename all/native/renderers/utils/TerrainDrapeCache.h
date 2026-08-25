/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINDRAPECACHE_H_
#define _MASSIF_TERRAINDRAPECACHE_H_

#include <cstddef>
#include <map>
#include <vector>

#include <vt/TileId.h>

namespace massif {

    /**
     * Shared render-to-texture drape target for 3D terrain.
     *
     * Owns one offscreen framebuffer and a per-terrain-tile colour texture, ABOVE the tile
     * layers. That is the point: every drapeable tile layer bakes into the same texture for a
     * given tile, in layer order, so a hillshade layer and a vector tile layer share one drape,
     * one terrain surface draw and one depth domain instead of each keeping their own.
     *
     * Textures are keyed by (tile, stack). Stack 0 is the RGBA colour drape - the only one the
     * stack index was originally meant to have, a second one being reserved for a run of drapeable
     * layers split by a non-drapeable one, which nothing produces. Stacks 1..K are now the R8
     * COVERAGE MASKS a live no-drape layer is occluded by (#175) - one per cut in the style order,
     * baked and invalidated with the colour drape they belong to.
     *
     * GL thread only.
     */
    class TerrainDrapeCache {
    public:
        TerrainDrapeCache();
        ~TerrainDrapeCache();

        int getResolution() const;
        /**
         * Sets the per-tile texture resolution. Existing textures are dropped, since they are
         * the old size.
         */
        void setResolution(int resolution);

        /**
         * Identifies the set of layers the textures are baked from. A tile's fingerprint only
         * covers the CONTENT of the layers that are present, so replacing a layer (switching the
         * base map's style rebuilds the layer) or removing one leaves every cached texture holding
         * a picture of a stack that no longer exists - and those textures stay cached, off screen,
         * until panning brings them back. That is the old style flashing back tile by tile.
         * Changing the signature marks every entry stale: it may still be shown (it is better than
         * a flat fill) but it is re-baked with priority and never seeds or stands in for another
         * tile, so old content cannot spread into tiles that never had it.
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
         * Returns the texture for a tile, creating or recycling one if needed.
         * needsBake is set when the texture does not match the given fingerprint, i.e. the caller
         * should clear it and have every participating layer bake into it.
         * hasContent is set when the texture has been baked at least once and is safe to sample -
         * a recycled texture still holds another tile's picture until it is baked, so a caller
         * that skips the bake (budget) must not draw it.
         */
        unsigned int acquire(const vt::TileId& tileId, int stack, std::size_t fingerprint, bool& needsBake, bool& hasContent);
        /**
         * Records that the caller actually baked this tile. Marking on acquire instead would
         * poison the entry for good on any path that acquires and then does not bake.
         * layerMask is the set of drape layers that actually put something in the texture.
         */
        // What the cached drape textures may cost in total. Public because the automatic bake
        // resolution is chosen against it (TileRenderer::resolveDrapeResolution): the two have to
        // agree, or the cache evicts what the resolution assumed would stay.
        static const std::size_t MAX_BYTES;
        // debug.massif.drapebudget 0 restores the pre-budget behaviour - a tile COUNT cap and an
        // uncapped bake resolution - so the two can be measured against each other in one build.
        static bool isBudgetEnabled();
        // debug.massif.drapemask 0 turns the no-drape occlusion masks off (#175), so a run with a
        // live layer back on top of the whole drape is one relaunch away. Read once (Android only).
        static bool isCoverageMaskEnabled();

        /**
         * Rebuilds the mipmap chain of a drape texture. Must follow every write to its level 0,
         * which is what the tile surfaces then sample minified.
         */
        static void generateMipmaps(unsigned int texture);
        static bool isMipmapEnabled();

        void markBaked(const vt::TileId& tileId, int stack, std::size_t fingerprint, std::size_t layerMask);
        /**
         * The layers the cached texture was baked from, or 0 if it has never been baked. A tile
         * baked before one of its layers had loaded shows that layer's ground missing - visually
         * a hillshade-only patch among finished tiles - so it is worth re-baking sooner than a
         * tile whose content merely moved on.
         */
        std::size_t bakedLayerMask(const vt::TileId& tileId, int stack) const;
        /**
         * Records that the caller filled this tile's texture from other cached tiles (a magnified
         * ancestor, or the finer tiles it replaces) rather than by baking it. The texture is then
         * safe to sample - it shows the right ground - but it is NOT a bake: it still needs one,
         * and it must never become a source for another seed, or the picture degrades every time
         * it is copied.
         */
        void markSeeded(const vt::TileId& tileId, int stack);
        /**
         * Whether the texture holds a real bake, as opposed to nothing or a seed.
         */
        bool isBaked(const vt::TileId& tileId, int stack) const;
        /**
         * Returns the texture of an already-baked tile, or 0. Used to let a tile whose own bake
         * has not landed yet stand in on an ancestor's texture instead of flashing a flat colour.
         * A tile found this way counts as USED for this frame: it is about to be drawn, and an
         * entry that is drawn but not marked would be evicted as unused at the end of the very
         * frame it stood in on - which is what emptied the cache of the previous generation on
         * every integer zoom step and left a screen of flat fills behind.
         */
        unsigned int findBaked(const vt::TileId& tileId, int stack);
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
        unsigned int createTexture(bool mask);
        std::size_t cachedBytes() const;

        static const int MAX_ANISOTROPY;
        static const std::size_t MAX_POOLED_TEXTURES; // recycled textures kept between frames
        static const std::size_t MAX_ENTRIES;         // cached tiles kept alive across frames (upper bound)
        static const std::size_t MIN_ENTRIES;         // ... but never fewer than this, whatever the resolution costs
        std::size_t maxEntries() const;

        int _resolution;
        std::size_t _stackSignature;
        unsigned int _frameBuffer;
        std::map<Key, Entry> _entries;
        std::vector<unsigned int> _texturePool;
        std::vector<unsigned int> _maskTexturePool; // R8, kept apart: a pooled texture keeps its format
        unsigned int _frameCounter;
    };

}

#endif

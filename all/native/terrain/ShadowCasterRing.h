/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SHADOWCASTERRING_H_
#define _MASSIF_SHADOWCASTERRING_H_

#include <algorithm>
#include <cstddef>
#include <set>
#include <tuple>
#include <vector>

namespace massif {

    /**
     * The zoom the shadow caster ring is generated at, and its tile grid.
     * See MapRenderer::applyTerrainShadows and docs/internals/rendering/09-shadows.md.
     */
    struct ShadowCasterRing {
        /** The cover's footprint at one zoom, before the margin is added. */
        struct Grid {
            int zoom = 0;
            int minX = 0, minY = 0, maxX = 0, maxY = 0;
        };

        /** How many tiles the ring generates for this footprint, margin included. */
        static std::size_t tileCount(const Grid& grid, int margin) {
            std::size_t width = static_cast<std::size_t>(grid.maxX - grid.minX + 1 + 2 * margin);
            std::size_t height = static_cast<std::size_t>(grid.maxY - grid.minY + 1 + 2 * margin);
            return width * height;
        }

        /**
         * Coarsens the ring until its grid fits maxTiles: over flat ground the throw is 0, so a tilted cover
         * reaching the horizon would be thousands of tiles a side at its finest zoom. The caller's
         * subdivision brings the resolution back where the cover is finer.
         */
        static Grid fit(const Grid& grid, int margin, std::size_t maxTiles) {
            Grid fitted = grid;
            while (fitted.zoom > 0 && tileCount(fitted, margin) > maxTiles) {
                fitted.zoom--;
                fitted.minX >>= 1; fitted.maxX >>= 1;
                fitted.minY >>= 1; fitted.maxY >>= 1;
            }
            return fitted;
        }

        struct Tile {
            int zoom = 0, x = 0, y = 0;
            bool operator<(const Tile& other) const { return std::tie(zoom, x, y) < std::tie(other.zoom, other.x, other.y); }
        };

        /**
         * Neighbours of the visible tiles on the sun's side (sideX/sideY: -1, 0 or +1 along the tile grid), from
         * minZoom, overlapping none of them: their extrusions throw shadows into the view. mapbox's
         * extendTileCover(direction) (geo/transform.ts), with the overlap test a mixed-zoom cover needs.
         */
        static std::vector<Tile> sunwardTiles(const std::vector<Tile>& visible, int sideX, int sideY, int minZoom) {
            std::vector<Tile> tiles;
            if (sideX == 0 && sideY == 0) {
                return tiles;
            }
            std::set<Tile> visibleSet(visible.begin(), visible.end()), visibleAncestors;
            for (const Tile& tile : visible) {
                for (int zoom = tile.zoom - 1; zoom >= 0; zoom--) {
                    visibleAncestors.insert(Tile { zoom, tile.x >> (tile.zoom - zoom), tile.y >> (tile.zoom - zoom) });
                }
            }
            auto overlapsView = [&](const Tile& tile) {
                if (visibleSet.count(tile) || visibleAncestors.count(tile)) {
                    return true;
                }
                for (int zoom = tile.zoom - 1; zoom >= 0; zoom--) {
                    if (visibleSet.count(Tile { zoom, tile.x >> (tile.zoom - zoom), tile.y >> (tile.zoom - zoom) })) {
                        return true;
                    }
                }
                return false;
            };
            std::set<Tile> added;
            const int offsets[3][2] = { { sideX, 0 }, { 0, sideY }, { sideX, sideY } };
            for (const Tile& tile : visible) {
                if (tile.zoom < minZoom) {
                    continue;
                }
                for (const auto& offset : offsets) {
                    Tile caster { tile.zoom, tile.x + offset[0], tile.y + offset[1] };
                    if ((offset[0] == 0 && offset[1] == 0) || caster.y < 0 || caster.y >= (1 << caster.zoom) || overlapsView(caster)) {
                        continue;
                    }
                    if (added.insert(caster).second) {
                        tiles.push_back(caster);
                    }
                }
            }
            return tiles;
        }
    };

}

#endif

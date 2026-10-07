/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SHADOWCASTERRING_H_
#define _MASSIF_SHADOWCASTERRING_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <tuple>
#include <vector>

namespace massif {

    /**
     * The shadow caster tiles around the cover.
     * See MapRenderer::applyTerrainShadows and docs/internals/rendering/08-lighting-sky-fog.md.
     */
    struct ShadowCasterRing {
        struct Tile {
            int zoom = 0, x = 0, y = 0;
            bool operator<(const Tile& other) const { return std::tie(zoom, x, y) < std::tie(other.zoom, other.x, other.y); }
        };

        /**
         * Each cover tile's neighbours out to the shadow throw (a fraction of the world width), at its own zoom or ringZoom
         * if coarser: 1 to maxMargin tiles, not a fixed margin or the cover's bounding box (08-lighting-sky-fog.md).
         */
        static std::vector<Tile> ringCandidates(const std::vector<Tile>& cover, int ringZoom, int maxMargin, double throwFraction) {
            std::set<Tile> added;
            std::vector<Tile> tiles;
            for (const Tile& tile : cover) {
                int zoom = std::min(tile.zoom, ringZoom);
                int shift = tile.zoom - zoom;
                int margin = std::min(maxMargin, std::max(1, static_cast<int>(std::ceil(throwFraction * static_cast<double>(1 << zoom)))));
                for (int dy = -margin; dy <= margin; dy++) {
                    for (int dx = -margin; dx <= margin; dx++) {
                        Tile candidate { zoom, (tile.x >> shift) + dx, (tile.y >> shift) + dy };
                        if (candidate.y < 0 || candidate.y >= (1 << zoom)) {
                            continue;
                        }
                        if (added.insert(candidate).second) {
                            tiles.push_back(candidate);
                        }
                    }
                }
            }
            return tiles;
        }

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

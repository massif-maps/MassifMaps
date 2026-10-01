/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_NEARPLANECOVER_H_
#define _MASSIF_NEARPLANECOVER_H_

#include <cglib/vec.h>

#include <cmath>
#include <utility>
#include <vector>

namespace massif {

    /**
     * mapbox's extendTileCoverToNearPlane (transform.ts): a building standing on a tile below the frame rises
     * into it, so a layer with extrusions adds the tiles under the frustum's two bottom edges, from the near
     * plane to the ground, within the 3x3 tiles around the near corner. All points in tile units at one zoom.
     */
    struct NearPlaneCover {
        /** Where the bottom edge from the near corner meets the ground; false for an edge that never comes down. */
        static bool projectToGround(const cglib::vec3<double>& nearPoint, const cglib::vec3<double>& farPoint, double groundZ, cglib::vec2<double>& ground) {
            if (!(farPoint(2) < nearPoint(2)) || !(nearPoint(2) > groundZ)) {
                return false;
            }
            double t = (nearPoint(2) - groundZ) / (nearPoint(2) - farPoint(2));
            ground = cglib::vec2<double>(nearPoint(0) + (farPoint(0) - nearPoint(0)) * t, nearPoint(1) + (farPoint(1) - nearPoint(1)) * t);
            return true;
        }

        /** Tiles (x, y) of a numTiles-wide grid the edge e1 -> e2 crosses, in the 3x3 block around e1's tile. */
        static void edgeTiles(const cglib::vec2<double>& e1, const cglib::vec2<double>& e2, int numTiles, std::vector<std::pair<int, int> >& tiles) {
            int e1X = static_cast<int>(std::floor(e1(0))), e1Y = static_cast<int>(std::floor(e1(1)));
            for (int dx = -1; dx <= 1; dx++) {
                int x = e1X + dx;
                if (x < 0 || x >= numTiles) {
                    continue;
                }
                for (int dy = -1; dy <= 1; dy++) {
                    int y = e1Y + dy;
                    if (y < 0 || y >= numTiles) {
                        continue;
                    }
                    std::pair<int, int> tile(x, y);
                    bool known = false;
                    for (const std::pair<int, int>& other : tiles) {
                        known = known || other == tile;
                    }
                    if (!known && edgeIntersectsBox(e1, e2, cglib::vec2<double>(x, y), cglib::vec2<double>(x + 1, y + 1))) {
                        tiles.push_back(tile);
                    }
                }
            }
        }

        // mapbox intersection_tests.ts edgeIntersectsBox
        static bool edgeIntersectsBox(const cglib::vec2<double>& e1, const cglib::vec2<double>& e2, const cglib::vec2<double>& min, const cglib::vec2<double>& max) {
            if ((e1(0) < min(0) && e2(0) < min(0)) || (e1(0) > max(0) && e2(0) > max(0)) || (e1(1) < min(1) && e2(1) < min(1)) || (e1(1) > max(1) && e2(1) > max(1))) {
                return false;
            }
            const cglib::vec2<double> corners[4] = { min, cglib::vec2<double>(max(0), min(1)), max, cglib::vec2<double>(min(0), max(1)) };
            bool dir = isCounterClockwise(e1, e2, corners[0]);
            return dir != isCounterClockwise(e1, e2, corners[1]) || dir != isCounterClockwise(e1, e2, corners[2]) || dir != isCounterClockwise(e1, e2, corners[3]);
        }

        static bool isCounterClockwise(const cglib::vec2<double>& a, const cglib::vec2<double>& b, const cglib::vec2<double>& c) {
            return (c(1) - a(1)) * (b(0) - a(0)) > (b(1) - a(1)) * (c(0) - a(0));
        }
    };

}

#endif

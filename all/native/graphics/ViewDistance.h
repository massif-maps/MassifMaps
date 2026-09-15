/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VIEWDISTANCE_H_
#define _MASSIF_VIEWDISTANCE_H_

#include <algorithm>
#include <cmath>

namespace massif {

    /**
     * The camera's own distance, which everything about how far the map reaches is a multiple of.
     * Free of ViewState so the host tests can reach it; see ViewState::calculateViewDistances and
     * docs/internals/rendering/05-depth-model.md.
     */
    struct ViewDistance {
        /** Tangram's LOD depth: 2^(d+1)-1 = 127 tile widths, the cap on how far tiles are walked. */
        static const int MAX_TILE_LOD = 6;

        /**
         * Tangram's m_pos.z: the zoom-derived orbit distance, or the camera's height above sea
         * level when that is larger. On a summit the two part company, and the orbit alone draws a
         * few kilometres of panorama.
         */
        static double cameraHeight(double orbitDistance, double cameraAltitude) {
            return std::max(orbitDistance, cameraAltitude);
        }

        /** 127 tile widths at this zoom - tangram's guard on how many tiles a walk can ever reach. */
        static double tileWalkCap(double worldTileSize) {
            return worldTileSize * (static_cast<double>(1 << (MAX_TILE_LOD + 1)) - 1.0);
        }

        /**
         * Below this the tilt no longer lengthens the ceiling: 0.1 is tilt 5.7 degrees, already 10x
         * the straight-down reach, and past it the tile walk cap is the bound in any case.
         */
        static constexpr double MIN_TILT_SIN = 0.1;

        /**
         * The ceiling on how far the map is DRAWN: DrawDistance multiples of that height, divided
         * by the sine of the tilt. Scaling the ORBIT alone made it a function of the zoom, so
         * dropping near the ground in mountains cut off peaks whose tiles were being fetched anyway.
         *
         * The tilt term is mapbox's cameraToSeaLevelDistance = altitude / cos(pitch)
         * (src/geo/projection/far_z.ts); pitch 0 is straight down there and tilt 90 is straight down
         * here, so their cos(pitch) is our sin(tilt) and straight down is unchanged. Without it the
         * map reaches as far looking at the horizon as at the camera's own feet, which is why a low
         * camera saw a hundred metres of a mountain range.
         *
         * Under the tile walk cap all the same: with ViewDistanceFactor 0 the cull envelope stops at
         * the far plane ALONE, so an unbounded ceiling is an unbounded tile walk (CullWorker).
         */
        static double drawCeiling(double cameraHeight, double drawDistance, double worldTileSize, double tiltSin) {
            double grazing = std::max(std::isfinite(tiltSin) ? tiltSin : 1.0, MIN_TILT_SIN);
            return std::min(cameraHeight * drawDistance / grazing, tileWalkCap(worldTileSize));
        }
    };

}

#endif

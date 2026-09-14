/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VIEWDISTANCE_H_
#define _MASSIF_VIEWDISTANCE_H_

#include <algorithm>

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
         * The ceiling on how far the map is DRAWN: DrawDistance multiples of that height. Scaling
         * the ORBIT alone made it a function of the zoom, so dropping near the ground in mountains
         * cut off peaks whose tiles were being fetched anyway. Mapbox has no zoom-only ceiling at
         * all; its far plane is 10 * cameraAltitude / cos(pitch) (src/geo/projection/far_z.ts).
         *
         * Under the tile walk cap all the same: with ViewDistanceFactor 0 the cull envelope stops at
         * the far plane ALONE, so an unbounded ceiling is an unbounded tile walk (CullWorker).
         */
        static double drawCeiling(double cameraHeight, double drawDistance, double worldTileSize) {
            return std::min(cameraHeight * drawDistance, tileWalkCap(worldTileSize));
        }
    };

}

#endif

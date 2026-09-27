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
     * The camera height every view distance is a multiple of; free of ViewState for the host tests.
     * See docs/internals/rendering/05-depth-model.md.
     */
    struct ViewDistance {
        /** Tangram's LOD depth: 2^(d+1)-1 = 127 tile widths, the cap on how far tiles are walked. */
        static const int MAX_TILE_LOD = 6;

        /**
         * Tangram's m_pos.z: the orbit distance, or the altitude above sea level when larger
         * (on a summit the orbit alone draws only a few kilometres).
         */
        static double cameraHeight(double orbitDistance, double cameraAltitude) {
            return std::max(orbitDistance, cameraAltitude);
        }

        /** 127 tile widths at this zoom - tangram's guard on how many tiles a walk can ever reach. */
        static double tileWalkCap(double worldTileSize) {
            return worldTileSize * (static_cast<double>(1 << (MAX_TILE_LOD + 1)) - 1.0);
        }

        /**
         * Below this the tilt no longer lengthens the ceiling (tilt 5.7 degrees); the tile walk cap bounds it anyway.
         */
        static constexpr double MIN_TILT_SIN = 0.1;

        /**
         * How far the map is drawn: DrawDistance camera heights over sin(tilt) (mapbox far_z.ts),
         * under the tile walk cap because with ViewDistanceFactor 0 nothing else bounds the tile walk.
         */
        static double drawCeiling(double cameraHeight, double drawDistance, double worldTileSize, double tiltSin) {
            double grazing = std::max(std::isfinite(tiltSin) ? tiltSin : 1.0, MIN_TILT_SIN);
            return std::min(cameraHeight * drawDistance / grazing, tileWalkCap(worldTileSize));
        }
    };

}

#endif

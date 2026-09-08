/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_EXTRUSIONFLOOR_H_
#define _MASSIF_VT_EXTRUSIONFLOOR_H_

namespace massif::vt {

    /**
     * Which few of a footprint's vertices the drawn ground under a building is read at.
     *
     * The floor is the MAX ground over the footprint, and asking every vertex for it was 3 M
     * elevation queries a second while panning (performance-log 26). Eight support points bound it
     * instead - and they are footprint vertices, unlike a bbox corner, which is why mapbox's
     * corner-sampled lift is not what we take here (04-terrain.md).
     */
    struct ExtrusionFloor {
        static constexpr int SUPPORT_DIRECTIONS = 8;

        /**
         * How far a point reaches along one support direction. The vertex scoring highest is that
         * direction's support point. The diagonals are the ones a bbox misses: a building at 45
         * degrees has its extremes there and its axis-aligned box corners on its neighbours.
         */
        static float supportScore(int direction, float x, float y) {
            switch (direction) {
            case 0: return x;
            case 1: return -x;
            case 2: return y;
            case 3: return -y;
            case 4: return x + y;
            case 5: return x - y;
            case 6: return -x + y;
            default: return -x - y;
            }
        }
    };

}

#endif

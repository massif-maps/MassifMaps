/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_EXTRUSIONFLOOR_H_
#define _MASSIF_VT_EXTRUSIONFLOOR_H_

namespace massif::vt {

    /**
     * Which few of a footprint's vertices the drawn ground under a building is read at: the floor is the max
     * ground over eight support points, footprint vertices unlike mapbox's bbox corners (04-terrain.md).
     */
    struct ExtrusionFloor {
        static constexpr int SUPPORT_DIRECTIONS = 8;

        /**
         * How far a point reaches along one support direction; the highest-scoring vertex is its support point.
         * The diagonals catch the extremes of a building at 45 degrees, which a bbox misses.
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

/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SMOOTHBASELEVEL_H_
#define _MASSIF_SMOOTHBASELEVEL_H_

#include "core/MapTile.h"

#include <algorithm>
#include <cmath>

namespace massif {

    /**
     * Levels between a grid of `posting` metres and the level a building base reads its ground at
     * (`targetPosting`): positive = the grid is finer than wanted, negative = coarser.
     */
    inline int smoothBaseLevelOffset(double posting, double targetPosting) {
        return (posting > 0 ? static_cast<int>(std::ceil(std::log(targetPosting / posting) / std::log(2.0))) : 0);
    }

    /**
     * The elevation tile a building base must wait for when the grid answering it (at gridZoom, `offset`
     * from smoothBaseLevelOffset) is more than maxAncestorLevels too coarse; zoom -1 when the answer stands.
     * x, y: the tile holding the point at `zoom`.
     */
    inline MapTile smoothBaseWantedTile(int x, int y, int zoom, int gridZoom, int offset, int maxAncestorLevels) {
        if (-offset <= maxAncestorLevels) {
            return MapTile(0, 0, -1, 0);
        }
        int wantedZoom = std::min(zoom, gridZoom - offset);
        return MapTile(x >> (zoom - wantedZoom), y >> (zoom - wantedZoom), wantedZoom, 0);
    }

}

#endif

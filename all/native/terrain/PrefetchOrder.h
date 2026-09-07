/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_PREFETCHORDER_H_
#define _MASSIF_PREFETCHORDER_H_

#include "core/MapTile.h"

#include <cmath>

namespace massif {

    /**
     * How far a queued elevation tile is from the point prefetching orders against - both in
     * normalised mercator (u east, v south, 0..1 over the world), the tile taken at its centre.
     * Kept out of ElevationManager so the ordering can be tested on the host: the manager itself
     * drags in the data source and the grid cache and cannot be linked there.
     *
     * The result is in TILE WIDTHS at the tile's own zoom, not in mercator units, because the
     * queue mixes levels: a coarse ancestor covering the focus and a fine tile sitting on it are
     * both "the ground under the camera" and must rank alike, which raw mercator distance would
     * not do - it would put the ancestor's centre up to eight tiles away.
     */
    inline double prefetchTileDistance(const MapTile& tile, double focusU, double focusV) {
        double extent = static_cast<double>(1 << tile.getZoom());
        double du = (tile.getX() + 0.5) / extent - focusU;
        du -= std::floor(du + 0.5); // the short way round, for a view sitting on the antimeridian
        double dv = (tile.getY() + 0.5) / extent - focusV; // mercator y does not wrap
        return std::sqrt(du * du + dv * dv) * extent;
    }

}

#endif

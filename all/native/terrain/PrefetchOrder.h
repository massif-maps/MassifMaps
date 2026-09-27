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
     * Distance of a queued elevation tile's centre from the prefetch focus (normalised mercator, u east, v south),
     * in tile widths at the tile's own zoom: the queue mixes levels, and a coarse ancestor covering the focus
     * must rank like a fine tile on it.
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

/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_PREFETCHORDER_H_
#define _MASSIF_PREFETCHORDER_H_

#include "core/MapTile.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

    /**
     * One shared level of ancestors over `dataTiles`, `levelsUp` above the coarsest and not below `minZoom`:
     * the grid lookup walks up only, so a view collapses to the one or two fetches that give every tile a height.
     */
    inline std::vector<MapTile> coarseCoverTiles(const std::vector<MapTile>& dataTiles, int levelsUp, int minZoom) {
        std::vector<MapTile> result;
        if (dataTiles.empty()) {
            return result;
        }
        int zoom = dataTiles.front().getZoom();
        for (const MapTile& tile : dataTiles) {
            zoom = std::min(zoom, tile.getZoom());
        }
        zoom = std::max(minZoom, zoom - levelsUp);
        for (const MapTile& tile : dataTiles) {
            if (tile.getZoom() < zoom) {
                continue;
            }
            int dz = tile.getZoom() - zoom;
            MapTile ancestor(tile.getX() >> dz, tile.getY() >> dz, zoom, 0);
            if (std::find(result.begin(), result.end(), ancestor) == result.end()) {
                result.push_back(ancestor);
            }
        }
        return result;
    }

}

#endif

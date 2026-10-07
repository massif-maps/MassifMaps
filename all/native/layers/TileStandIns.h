/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILESTANDINS_H_
#define _MASSIF_TILESTANDINS_H_

#include "core/MapTile.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <vector>

namespace massif {

    /**
     * maplibre-native's prefetchZoomDelta for a mixed-zoom cover: each visible tile's ancestor zoomDelta levels up, never
     * under minZoom, once each. Kept loaded, they stand in for a tile turned into view while it loads (02-tiles.md).
     */
    inline std::vector<MapTile> calculateStandInTiles(const std::vector<MapTile>& visibleTiles, int zoomDelta, int minZoom) {
        std::vector<MapTile> standInTiles;
        std::set<std::tuple<int, int, int> > taken;
        for (const MapTile& tile : visibleTiles) {
            int zoom = std::max(minZoom, tile.getZoom() - zoomDelta);
            if (zoom >= tile.getZoom()) {
                continue;
            }
            int shift = tile.getZoom() - zoom;
            int tileMask = (1 << zoom) - 1;
            int x = (tile.getX() >> shift) & tileMask;
            int y = (tile.getY() >> shift) & tileMask;
            if (taken.emplace(zoom, x, y).second) {
                standInTiles.emplace_back(x, y, zoom, tile.getFrameNr());
            }
        }
        return standInTiles;
    }

}

#endif

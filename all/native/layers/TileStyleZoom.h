/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILESTYLEZOOM_H_
#define _MASSIF_TILESTYLEZOOM_H_

#include <algorithm>

namespace massif {

    /**
     * The zoom a tile's STYLE evaluates at: the zoom the camera asked for, not the (possibly
     * coarser) tile the LOD handed back. CartoCSS [zoom] gates on the tile, so without a lift a
     * single level of LOD coarsening drops every rule written for the camera's zoom.
     * The lift is bounded (Options::TileStyleZoomLift) because a horizon tile styled at the
     * camera's zoom emits the whole near-field content over ground tens of times wider.
     */
    inline int calculateStyleTileZoom(int tileZoom, int targetTileZoom, int maxZoomLift) {
        if (targetTileZoom <= tileZoom || targetTileZoom - tileZoom > maxZoomLift) {
            return tileZoom; // at or past the ring: the horizon pays nothing for the lift
        }
        return targetTileZoom;
    }

}

#endif

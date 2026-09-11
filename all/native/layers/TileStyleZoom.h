/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILESTYLEZOOM_H_
#define _MASSIF_TILESTYLEZOOM_H_

#include <algorithm>
#include <cmath>

namespace massif {

    /**
     * The tile zoom the camera asks for, held across the boundary by a margin. In terrain mode the
     * focus rides the ground, so the zoom drifts by a fraction of a level whenever the elevation
     * under it moves - and changing this re-decodes every visible tile. Measured at Zermatt, one
     * 2D/3D switch at zoom 12.05 drifted to 11.95 and back: invisible, and it re-decoded the whole
     * map twice on top of the switch's own decode.
     * cameraZoom already carries the LOD offset and the layer's zoom level bias.
     */
    inline int calculateTargetTileZoom(double cameraZoom, int currentTarget, double hysteresis) {
        int candidate = static_cast<int>(std::floor(cameraZoom));
        if (currentTarget < 0 || candidate == currentTarget) {
            return candidate;
        }
        if (candidate > currentTarget) {
            return cameraZoom >= currentTarget + 1 + hysteresis ? candidate : currentTarget;
        }
        return cameraZoom <= currentTarget - hysteresis ? candidate : currentTarget;
    }

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

    /**
     * Whether a tile decoded at styleTileZoom still matches what the camera asks for. The style zoom
     * is snapshotted when the fetch is QUEUED, and a target-zoom change invalidates the cache but not
     * the tasks in flight - those land after it and, being fresh, count as valid. That is how a tile
     * decoded for zoom 13 survived a zoom-out to 11 and kept drawing its [zoom>=12] contours.
     */
    inline bool isStyleTileZoomCurrent(int tileZoom, int styleTileZoom, int targetTileZoom, int maxZoomLift) {
        return styleTileZoom == calculateStyleTileZoom(tileZoom, targetTileZoom, maxZoomLift);
    }

}

#endif

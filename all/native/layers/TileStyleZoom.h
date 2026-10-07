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
     * The tile zoom the camera asks for, held across the boundary by a margin: with terrain the zoom
     * drifts with the ground under the focus, and every change re-decodes all visible tiles.
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
     * calculateTargetTileZoom while the view moves, the camera's own level once the same view is culled again: a margin
     * kept at rest left a camera at 12.05 on z11 tiles for good when it came from 11.5 (02-tiles.md). held: still off it.
     */
    inline int settleTargetTileZoom(double cameraZoom, int currentTarget, double hysteresis, bool sameView, bool& held) {
        int target = calculateTargetTileZoom(cameraZoom, currentTarget, sameView ? 0.0 : hysteresis);
        held = (target != static_cast<int>(std::floor(cameraZoom)));
        return target;
    }

    /**
     * The zoom a tile's style evaluates at: the camera's target, not the coarser LOD tile, so [zoom]
     * rules survive coarsening. Bounded by Options::TileStyleZoomLift so horizon tiles stay cheap.
     */
    inline int calculateStyleTileZoom(int tileZoom, int targetTileZoom, int maxZoomLift) {
        if (targetTileZoom <= tileZoom || targetTileZoom - tileZoom > maxZoomLift) {
            return tileZoom;
        }
        return targetTileZoom;
    }

    /**
     * Whether a tile decoded at styleTileZoom still matches the camera: in-flight tasks survive a
     * target-zoom change and land fresh, so validity alone cannot tell.
     */
    inline bool isStyleTileZoomCurrent(int tileZoom, int styleTileZoom, int targetTileZoom, int maxZoomLift) {
        return styleTileZoom == calculateStyleTileZoom(tileZoom, targetTileZoom, maxZoomLift);
    }

}

#endif

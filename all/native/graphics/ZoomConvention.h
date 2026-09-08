/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ZOOMCONVENTION_H_
#define _MASSIF_ZOOMCONVENTION_H_

#include <cmath>

namespace massif {

    /**
     * What a zoom NUMBER means: how far the camera sits from its focus.
     *
     * The SDK calibrates on a 256-pixel tile, maplibre and mapbox-gl on a 512-pixel one, so the
     * same number is a level apart. Options::ZoomOffset bridges them, and it is a RENUMBERING
     * only - see docs/maintenance/web-build.md for what else had to move, and what did not.
     */
    struct ZoomConvention {
        /**
         * The tile the CAMERA is calibrated on, in screen points. Not what the LOD measures against.
         * @param tileDrawSize Options::getTileDrawSize.
         * @param zoomOffset Options::getZoomOffset, in zoom levels.
         */
        static double cameraTileSize(double tileDrawSize, double zoomOffset) {
            return tileDrawSize * std::pow(2.0, zoomOffset);
        }

        /**
         * The zoom the RENDERER works in: vt sizes by `2^(zoom - tileZoom)`, so it needs the zoom
         * the tiles were chosen for, not the one the app reads.
         */
        static double renderZoom(double zoom, double zoomOffset) {
            return zoom + zoomOffset;
        }

        /**
         * The camera-to-focus distance at zoom 0: where one world tile covers one camera tile.
         * @param screenHeight The viewport height in pixels.
         * @param worldSize The width of the world in internal units (Const::WORLD_SIZE).
         * @param tanHalfFOVY The tangent of half the vertical field of view.
         */
        static double zoom0Distance(double screenHeight, double worldSize, double tileDrawSize,
                                    double zoomOffset, double tanHalfFOVY, double dpiScale) {
            double tilePixels = cameraTileSize(tileDrawSize, zoomOffset) * dpiScale;
            if (tilePixels <= 0 || tanHalfFOVY <= 0) {
                return 0;
            }
            return screenHeight * 0.5 * worldSize / (tilePixels * tanHalfFOVY);
        }
    };

}

#endif

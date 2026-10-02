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
     * What a zoom number means: how far the camera sits from its focus. The SDK calibrates on a 256-pixel tile, maplibre and
     * mapbox-gl on 512, so the same number is a level apart; Options::ZoomOffset only renumbers, see docs/maintenance/web-build.md.
     */
    struct ZoomConvention {
        /**
         * The tile the camera is calibrated on, in screen points. Not what the LOD measures against.
         * @param tileDrawSize Options::getTileDrawSize.
         * @param zoomOffset Options::getZoomOffset, in zoom levels.
         */
        static double cameraTileSize(double tileDrawSize, double zoomOffset) {
            return tileDrawSize * std::pow(2.0, zoomOffset);
        }

        /**
         * The zoom the renderer works in: vt sizes by `2^(zoom - tileZoom)`, so it needs the zoom
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

        /**
         * Internal units per screen pixel that a vector element's size is multiplied by. CARTO's formula scaled with
         * 1 / tan²(fovY / 2); sizes stay where they were at its 70° whatever the field of view.
         */
        static double unitToPixel(double zoom0Distance, double screenHeight, double tanHalfFOVY, double pow2Zoom) {
            const double tanHalf70 = std::tan(35.0 * 3.14159265358979323846 / 180.0);
            if (screenHeight <= 0 || pow2Zoom <= 0) {
                return 0;
            }
            return zoom0Distance * tanHalfFOVY / (screenHeight * tanHalf70 * tanHalf70 * pow2Zoom);
        }
    };

}

#endif

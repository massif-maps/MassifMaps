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
     * What a zoom NUMBER means: how far the camera sits, and how large the tile it is calibrated on
     * is drawn.
     *
     * Its own module because the camera distance was written out twice inside ViewState alone, and
     * because the offset below is easy to apply in one place too many.
     *
     * The SDK calibrates zoom the way a 256-pixel slippy map does: at zoom z one tile covers
     * `tileDrawSize` screen points. maplibre and mapbox-gl calibrate on a 512-pixel tile, so the
     * same number puts their camera one level closer. Options::ZoomOffset is the bridge.
     *
     * It is a RENUMBERING and nothing else. Only the camera distance moves: the level-of-detail
     * rule is a screen-area one and is left alone, so an offset of 1 at zoom 13 fetches the tiles
     * zoom 14 used to and draws them at the size it used to - the same picture, a different number.
     * Scaling the LOD budget with the camera was tried and is wrong: it fetches a level coarser and
     * draws it twice as large, which doubles every label and line. That is the same trap as raising
     * TileDrawSize, and it is what this option exists to avoid. Measured both ways, see
     * docs/maintenance/web-build.md.
     */
    struct ZoomConvention {
        /**
         * The tile the CAMERA is calibrated on, in screen points. Not what the LOD measures
         * against - that is getTileDrawSize itself, see the note above.
         * @param tileDrawSize Options::getTileDrawSize.
         * @param zoomOffset Options::getZoomOffset, in zoom levels.
         */
        static double cameraTileSize(double tileDrawSize, double zoomOffset) {
            return tileDrawSize * std::pow(2.0, zoomOffset);
        }

        /**
         * The zoom the RENDERER works in. The vector tile renderer sizes everything by
         * `2^(zoom - tileZoom)`, so it has to be given the zoom the tiles were chosen for -
         * otherwise a renumbered camera leaves it believing the map is a level further out, and it
         * draws the same tile twice as large. Reported zoom is what an app sees; this is what vt
         * sees, and the two differ by the offset alone.
         */
        static double renderZoom(double zoom, double zoomOffset) {
            return zoom + zoomOffset;
        }

        /**
         * The camera-to-focus distance at zoom 0, which the whole zoom scale hangs off: the
         * distance at which one tile of the world covers exactly the camera tile. Every other zoom
         * is this over 2^zoom.
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

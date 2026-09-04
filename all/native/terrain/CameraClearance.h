/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CAMERACLEARANCE_H_
#define _MASSIF_CAMERACLEARANCE_H_

#include <algorithm>
#include <cmath>
#include <limits>

namespace massif {

    /**
     * How close the camera may get to the ground under it, and the zoom that lands on that shell.
     * mapbox's model (transform._minimumHeightOverTerrain / _constrainCamera): the clearance is a
     * FRACTION of the camera's distance to sea level, not a fixed height, so it never blocks a
     * zoom-in on its own. Free of the renderer on purpose, so it is testable on the host.
     * See docs/internals/rendering/04-terrain.md.
     */
    struct CameraClearance {
        // mapbox MAX_DRAPE_OVERZOOM: the clearance is the orbit at 4 zoom levels past the sea-level one.
        static constexpr double FRACTION = 1.0 / 16.0;

        /**
         * The minimum camera height above the ground under it. All values are internal units.
         * @param focusZ The ground height at the focus (mapbox _centerAltitude).
         * @param orbit The camera-to-focus distance, what the zoom is calibrated on.
         * @param maxZoomOrbit The orbit at the maximum zoom; the clearance never shrinks below its share.
         * @param floorZ An app's explicit minimum (TerrainOptions::CameraClearance), 0 for none.
         */
        static double minHeight(double focusZ, double orbit, double maxZoomOrbit, double floorZ) {
            // mapbox: mercatorZ(min(seaLevelZoom, maxZoom) + 4), seaLevelZoom being the zoom whose
            // orbit is focusZ + orbit.
            double seaLevelOrbit = std::max(0.0, std::max(focusZ + orbit, maxZoomOrbit));
            return std::max(seaLevelOrbit * FRACTION, floorZ);
        }

        /**
         * The zoom at which the camera, zooming about the focus, lands on the clearance shell.
         * Below the current zoom when the camera is already under the shell; +infinity when no
         * zoom-in can bring it there (a camera at or below the focus rises with nothing).
         * @param zoom The current zoom.
         * @param focusZ The ground height at the focus.
         * @param cameraZ The camera height, at the current zoom.
         * @param orbit The camera-to-focus distance, at the current zoom.
         * @param terrainZ The ground height under the camera, taken as constant over the zoom.
         * @param maxZoomOrbit The orbit at the maximum zoom.
         * @param floorZ An app's explicit minimum clearance, 0 for none.
         */
        static float maxZoom(float zoom, double focusZ, double cameraZ, double orbit, double terrainZ, double maxZoomOrbit, double floorZ) {
            // A zoom scales the camera-to-focus vector by s, so the camera height is
            // focusZ + s * hz and its clearance above terrainZ must reach
            // max(FRACTION * (focusZ + s * orbit), c): two linear constraints on s, each a lower
            // bound when its slope is positive, and no bound at all when it is not.
            double hz = cameraZ - focusZ;
            double c = std::max(std::max(0.0, maxZoomOrbit) * FRACTION, floorZ);
            double sMin = 0;
            auto bound = [&](double slope, double rhs) {
                if (slope > 0) {
                    sMin = std::max(sMin, rhs / slope);
                }
            };
            bound(hz - orbit * FRACTION, terrainZ - focusZ + focusZ * FRACTION);
            bound(hz, terrainZ - focusZ + c);
            if (!(sMin > 0)) {
                return std::numeric_limits<float>::infinity();
            }
            return zoom - static_cast<float>(std::log2(sMin));
        }
    };

}

#endif

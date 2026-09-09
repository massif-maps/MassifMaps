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
     *
     * ONE divergence from mapbox, deliberate: mapbox builds its sea-level distance from
     * _centerAltitude + cameraToCenterDistance, the ORBIT, which is the camera's altitude only at
     * pitch 0. We use the camera's real altitude, so the shell does not grow by 1/sin(tilt) as the
     * view lies down and lift a camera that is plainly clear of the ground.
     * See docs/internals/rendering/04-terrain.md.
     */
    struct CameraClearance {
        // mapbox MAX_DRAPE_OVERZOOM: the clearance is the orbit at 4 zoom levels past the sea-level one.
        static constexpr double FRACTION = 1.0 / 16.0;

        /**
         * The minimum camera height above the ground under it. All values are internal units.
         * @param cameraZ The camera height above sea level.
         * @param maxZoomOrbit The orbit at the maximum zoom; the clearance never shrinks below its share.
         * @param floorZ An app's explicit minimum (TerrainOptions::CameraClearance), 0 for none.
         */
        static double minHeight(double cameraZ, double maxZoomOrbit, double floorZ) {
            return std::max(std::max(0.0, std::max(cameraZ, maxZoomOrbit)) * FRACTION, floorZ);
        }

        // How far above the shell the focus stops following the ground, in shells. Ours, not
        // mapbox's: they pin the centre to the terrain at every altitude, which makes a pan across
        // a ridge lift the whole camera with it - visible bobbing from far above the ground.
        static constexpr double FOLLOW_BAND = 4.0;

        /**
         * How much of the ground's height the FOCUS takes: all of it at the shell, none of it
         * FOLLOW_BAND shells above, linear in between.
         * @param clearance The camera's height above the ground under it, with the focus PINNED to
         *                  the ground - the lift itself moves the camera, so feeding the current
         *                  height back would oscillate.
         * @param minHeight The shell, from minHeight() above.
         */
        static double focusFollow(double clearance, double minHeight) {
            if (!(minHeight > 0) || !(clearance > minHeight)) {
                return 1.0;
            }
            return std::max(0.0, 1.0 - (clearance - minHeight) / (minHeight * (FOLLOW_BAND - 1)));
        }

        /**
         * The camera height ABOVE THE FOCUS that lands it on the shell. The shell moves with the
         * camera, so the lift is a fixed point, not terrainZ + minHeight: rising raises the
         * clearance it has to clear, and a lift that ignores that under-shoots every frame.
         * @param focusZ The ground height at the focus.
         * @param terrainZ The ground height under the camera.
         * @param maxZoomOrbit The orbit at the maximum zoom.
         * @param floorZ An app's explicit minimum clearance, 0 for none.
         */
        static double targetHeight(double focusZ, double terrainZ, double maxZoomOrbit, double floorZ) {
            return shellCameraZ(terrainZ, maxZoomOrbit, floorZ) - focusZ;
        }

        /**
         * The camera height above sea level that lands ON the shell over ground at `terrainZ`.
         * cameraZ - terrainZ >= max(FRACTION * cameraZ, c) - two lower bounds on cameraZ, both
         * gaining with it (FRACTION < 1), so the answer is the larger. It does not depend on the
         * focus, which is what lets the focus be moved to satisfy it.
         */
        static double shellCameraZ(double terrainZ, double maxZoomOrbit, double floorZ) {
            double c = std::max(std::max(0.0, maxZoomOrbit) * FRACTION, floorZ);
            return std::max(terrainZ / (1 - FRACTION), terrainZ + c);
        }

        /**
         * The zoom at which the camera, zooming about the focus, lands on the clearance shell.
         * Below the current zoom when the camera is already under the shell; +infinity when no
         * zoom-in can bring it there (a camera at or below the focus rises with nothing).
         * @param zoom The current zoom.
         * @param focusZ The ground height at the focus.
         * @param cameraZ The camera height, at the current zoom.
         * @param terrainZ The ground height under the camera, taken as constant over the zoom.
         * @param maxZoomOrbit The orbit at the maximum zoom.
         * @param floorZ An app's explicit minimum clearance, 0 for none.
         */
        static float maxZoom(float zoom, double focusZ, double cameraZ, double terrainZ, double maxZoomOrbit, double floorZ) {
            // A zoom scales the camera-to-focus vector by s, so the camera height is focusZ + s*hz
            // and its clearance must reach max(FRACTION * (focusZ + s*hz), c): two linear constraints
            // on s, each a lower bound only when its slope is positive.
            double hz = cameraZ - focusZ;
            double c = std::max(std::max(0.0, maxZoomOrbit) * FRACTION, floorZ);
            double sMin = 0;
            auto bound = [&](double slope, double rhs) {
                if (slope > 0) {
                    sMin = std::max(sMin, rhs / slope);
                }
            };
            bound(hz * (1 - FRACTION), terrainZ - focusZ + focusZ * FRACTION);
            bound(hz, terrainZ - focusZ + c);
            if (!(sMin > 0)) {
                return std::numeric_limits<float>::infinity();
            }
            return zoom - static_cast<float>(std::log2(sMin));
        }
    };

}

#endif

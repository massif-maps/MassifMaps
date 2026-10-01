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
     * Camera-to-ground clearance, mapbox's model (_minimumHeightOverTerrain): a fraction of the camera's
     * altitude, not a fixed height. Unlike mapbox we use the real altitude, not the orbit, so the shell
     * does not grow by 1/sin(tilt). See docs/internals/rendering/04-terrain.md.
     */
    struct CameraClearance {
        // mapbox MAX_DRAPE_OVERZOOM: the clearance is the orbit at 4 zoom levels past the sea-level one.
        static constexpr double FRACTION = 1.0 / 16.0;

        /**
         * The fraction an app asked for, or FRACTION when negative (so 0 stays expressible).
         * Settable because the fraction models an orbiting camera; a first-person app sets 0 for a fixed floor.
         */
        static double fractionOr(double fraction) {
            return fraction < 0 ? FRACTION : fraction;
        }

        /**
         * The minimum camera height above the ground under it. All values are internal units.
         * @param cameraZ The camera height above sea level.
         * @param maxZoomOrbit The orbit at the maximum zoom; the clearance never shrinks below its share.
         * @param floorZ An app's explicit minimum (TerrainOptions::CameraClearance), 0 for none.
         * @param fraction The share of the camera's altitude, negative for the default.
         */
        static double minHeight(double cameraZ, double maxZoomOrbit, double floorZ, double fraction = -1) {
            return std::max(std::max(0.0, std::max(cameraZ, maxZoomOrbit)) * fractionOr(fraction), floorZ);
        }

        /**
         * maplibre's recalculateZoomAndCenter, after a gesture held the focus height: the camera stays, the focus slides
         * along the view ray onto the ground, and the zoom follows the new distance. False for a ray that never meets it.
         * @param cameraZ The camera height.
         * @param dirZ The view direction's z (unit vector).
         * @param groundZ The height the focus is to land at.
         * @param distance The current camera-to-focus distance.
         * @param newDistance The camera-to-focus distance once on the ground.
         */
        static bool groundAlongView(double cameraZ, double dirZ, double groundZ, double distance, double& newDistance) {
            if (!(dirZ < -1.0e-6) || !(distance > 0) || !(cameraZ > groundZ)) {
                return false;
            }
            newDistance = (groundZ - cameraZ) / dirZ;
            return std::isfinite(newDistance) && newDistance > 0;
        }

        /**
         * The camera height above the focus that lands it on the shell. The shell moves with the camera,
         * so this is a fixed point, not terrainZ + minHeight, which would under-shoot every frame.
         * @param focusZ The ground height at the focus.
         * @param terrainZ The ground height under the camera.
         * @param maxZoomOrbit The orbit at the maximum zoom.
         * @param floorZ An app's explicit minimum clearance, 0 for none.
         */
        static double targetHeight(double focusZ, double terrainZ, double maxZoomOrbit, double floorZ, double fraction = -1) {
            return shellCameraZ(terrainZ, maxZoomOrbit, floorZ, fraction) - focusZ;
        }

        /**
         * The camera height above sea level that lands on the shell over ground at `terrainZ`: the larger
         * of the two lower bounds in cameraZ - terrainZ >= max(f * cameraZ, c). Independent of the focus.
         */
        static double shellCameraZ(double terrainZ, double maxZoomOrbit, double floorZ, double fraction = -1) {
            double f = fractionOr(fraction);
            double c = std::max(std::max(0.0, maxZoomOrbit) * f, floorZ);
            // At fraction 0 this collapses to terrainZ + floor, the fixed clearance.
            return std::max(terrainZ / (1 - f), terrainZ + c);
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
        static float maxZoom(float zoom, double focusZ, double cameraZ, double terrainZ, double maxZoomOrbit, double floorZ, double fraction = -1) {
            // A zoom scales the camera-to-focus vector by s, so the camera height is focusZ + s*hz
            // and its clearance must reach max(fraction * (focusZ + s*hz), c): two linear constraints
            // on s, each a lower bound only when its slope is positive.
            double f = fractionOr(fraction);
            double hz = cameraZ - focusZ;
            double c = std::max(std::max(0.0, maxZoomOrbit) * f, floorZ);
            double sMin = 0;
            auto bound = [&](double slope, double rhs) {
                if (slope > 0) {
                    sMin = std::max(sMin, rhs / slope);
                }
            };
            bound(hz * (1 - f), terrainZ - focusZ + focusZ * f);
            bound(hz, terrainZ - focusZ + c);
            if (!(sMin > 0)) {
                return std::numeric_limits<float>::infinity();
            }
            return zoom - static_cast<float>(std::log2(sMin));
        }
    };

}

#endif

/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CAMERANOOP_H_
#define _MASSIF_CAMERANOOP_H_

#include <cmath>

namespace massif {

    // A camera event that asks for the view the camera has still rewrites the matrices by float noise, and
    // the cull reads that as a move: the labels are placed again and blink. See 01-frame.md.
    namespace CameraNoOp {

        // Frames of an animation and instant calls; POS is a millionth of the viewport span.
        const double NOISE_POS = 1.0e-6;
        const float NOISE_ANGLE = 1.0e-4f; // degrees
        const float NOISE_ZOOM = 1.0e-5f;

        // The request of an animation: a GPS fix a fraction of a pixel from the camera is not worth a move.
        const double DEAD_BAND_TILE_PIXELS = 0.25;
        const float DEAD_BAND_ANGLE = 0.1f; // degrees
        const float DEAD_BAND_ZOOM = 1.0e-3f;

        inline double noiseTolerance(double spanAtZoom) {
            return NOISE_POS * spanAtZoom;
        }

        inline double deadBandTolerance(double worldSize, float zoom) {
            return DEAD_BAND_TILE_PIXELS * worldSize / (256.0 * std::pow(2.0, static_cast<double>(zoom)));
        }

        // The ground rule rewrites the focus height every frame, so it only counts (free roam) where nothing does.
        inline bool samePos(double dx, double dy, double dz, double tolerance, bool compareHeight) {
            return std::abs(dx) <= tolerance && std::abs(dy) <= tolerance && (!compareHeight || std::abs(dz) <= tolerance);
        }

        // Degrees.
        inline bool sameAngle(float delta, bool deadBand) {
            delta = std::abs(std::fmod(delta, 360.0f));
            float tolerance = deadBand ? DEAD_BAND_ANGLE : NOISE_ANGLE;
            return delta <= tolerance || delta >= 360.0f - tolerance;
        }

        inline bool sameZoom(float delta, bool deadBand) {
            return std::abs(delta) <= (deadBand ? DEAD_BAND_ZOOM : NOISE_ZOOM);
        }

    }

}

#endif

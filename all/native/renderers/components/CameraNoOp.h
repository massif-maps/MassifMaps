/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CAMERANOOP_H_
#define _MASSIF_CAMERANOOP_H_

#include <cmath>

namespace massif {

    /**
     * Whether a camera event leaves the view where it is. Applying one anyway rewrites the matrices
     * with float noise, which the cull reads as a moved camera: the tile set is recalculated
     * and the labels are placed again, so they blink on a camera that did not move.
     */
    namespace CameraNoOp {

        // A millionth of the screen width: far above the double rounding of a position round trip.
        const double POS_TOLERANCE = 1.0e-6;
        const float ANGLE_TOLERANCE = 1.0e-4f; // degrees
        const float ZOOM_TOLERANCE = 1.0e-5f;

        // spanAtZoom is the viewport's world span at the current zoom; 0 while the view has no size, which asks for equality.
        inline bool sameCoord(double coord0, double coord1, double spanAtZoom) {
            return std::abs(coord1 - coord0) <= POS_TOLERANCE * spanAtZoom;
        }

        // The deltas are the change an event asks for, in degrees and zoom levels.
        inline bool sameAngle(float delta) {
            delta = std::abs(std::fmod(delta, 360.0f));
            return delta <= ANGLE_TOLERANCE || delta >= 360.0f - ANGLE_TOLERANCE;
        }

        inline bool sameZoom(float delta) {
            return std::abs(delta) <= ZOOM_TOLERANCE;
        }

    }

}

#endif

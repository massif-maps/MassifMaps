/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_KINETICSTEP_H_
#define _MASSIF_KINETICSTEP_H_

#include <cmath>

namespace massif {

    /**
     * The fraction of its remaining delta a kinetic gesture consumes in one frame.
     */
    inline float kineticStepFraction(float slowdown, float deltaSeconds) {
        // The decay is wall-clock, so the step must be too, or fling distance scales with frame count.
        return 1.0f - std::pow(1.0f - slowdown, deltaSeconds);
    }

}

#endif

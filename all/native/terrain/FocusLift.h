/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FOCUSLIFT_H_
#define _MASSIF_FOCUSLIFT_H_

namespace massif {

    /**
     * How far the focus has to move in z to sit on the ground that is actually drawn. Free of the
     * renderer so the host tests can reach it; see docs/internals/rendering/04-terrain.md.
     *
     * The third case is the one that bites. Terrain lifts the focus onto the surface every frame,
     * and the zoom is calibrated on dist(camera, focus) - so a focus left at its last terrain height
     * after terrain is switched OFF describes a camera far nearer than it is: measured focusZ 107 m
     * over a ground back at z=0, drawing the map for a camera six times too close. Coarse tiles,
     * tiny labels, and ground that runs out into background.
     */
    inline double focusLiftDelta(bool haveElevation, bool heightKnown, double terrainZ, double focusZ) {
        if (!haveElevation) {
            return -focusZ; // no terrain: the ground is at z=0 and the focus comes back down to it
        }
        if (!heightKnown) {
            return 0.0; // nothing decoded under the focus yet - hold, rather than drop it to sea level
        }
        return terrainZ - focusZ;
    }

}

#endif

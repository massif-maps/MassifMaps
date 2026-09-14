/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINOCCLUSION_H_
#define _MASSIF_TERRAINOCCLUSION_H_

#include <algorithm>

namespace massif {

    /**
     * Whether a label anchor is BEHIND the terrain, given the terrain depths sampled around it.
     * Free of the renderer so the host tests can reach it; see TerrainRenderer::isOccludedByTerrain.
     *
     * All distances are view-space w, i.e. distance along the view axis.
     *
     * The relative tolerance alone is not enough, and that is a geometry problem rather than a tuning
     * one: an anchor sits on the terrain, but the SURFACE drawn under it differs by some vertical
     * error (the mesh chord, a coarser elevation level, the half-resolution read-back). Along the view
     * ray that vertical error becomes dz / sin(angle between ray and ground) - so at a grazing angle a
     * metre of height error is tens of metres of depth, and a label reads as behind its own ground.
     * Measured at Grenoble: POIs in the city and on the slope below La Bastille were dropped from a
     * low camera looking north, and reappeared as soon as the camera rose.
     *
     * The DEPTH SPREAD across the samples already measures that angle - it is how much the terrain's
     * distance changes over a few screen pixels - so the slack is taken from it rather than from a
     * separate normal or tilt term. Top-down, the spread is ~0 and the test is as tight as before.
     */
    struct TerrainOcclusion {
        /** Slack per unit of local depth spread. 1 = "within one neighbourhood's variation is not behind". */
        static constexpr float GRAZING_SLACK = 1.0f;

        /**
         * farthestW is the LARGEST terrain depth around the anchor and spreadW the largest minus the
         * smallest. Taking the farthest is what stops a label's own ground occluding it on a slope;
         * the spread is what stops the same thing at a grazing angle.
         */
        static bool isBehind(float labelW, float farthestW, float spreadW, float tolerance, float grazingSlack = GRAZING_SLACK) {
            float allowed = farthestW * tolerance + std::max(0.0f, spreadW) * grazingSlack;
            return labelW > allowed;
        }
    };

}

#endif

/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINOCCLUSION_H_
#define _MASSIF_TERRAINOCCLUSION_H_

#include <algorithm>
#include <cmath>

#include <cglib/vec.h>

namespace massif {

    /**
     * Whether a label anchor is behind the terrain; distances are view-space w. The depth spread widens
     * the slack at grazing angles, where a small height error becomes a large depth error.
     */
    struct TerrainOcclusion {
        /** Slack per unit of local depth spread. 1 = "within one neighbourhood's variation is not behind". */
        static constexpr float GRAZING_SLACK = 1.0f;

        /** farthestW is the largest terrain depth around the anchor, spreadW the largest minus the smallest. */
        static bool isBehind(float labelW, float farthestW, float spreadW, float tolerance, float grazingSlack = GRAZING_SLACK) {
            float allowed = farthestW * tolerance + std::max(0.0f, spreadW) * grazingSlack;
            return labelW > allowed;
        }

        /**
         * Whether the ground blocks the first maxFraction of the segment. groundAt(x, y, z) is false where
         * nothing is loaded; groundMargin is the target's own DEM cell, in horizontal units.
         */
        template <typename GroundFunc>
        static bool isSegmentBlocked(const cglib::vec3<double>& from, const cglib::vec3<double>& to, double maxFraction, double firstStep, double stepGrowth, double zTop, double groundMargin, GroundFunc groundAt) {
            // The target never below its own ground, read like the march: an anchor from another grid or
            // an earlier 2D/3D ramp step was hidden by the cell it stands in.
            cglib::vec3<double> target = to;
            double ground = 0;
            if (groundAt(to(0), to(1), ground)) {
                target(2) = std::max(to(2), ground);
            }
            double length = cglib::length(target - from);
            if (!(length > 0)) {
                return false;
            }
            cglib::vec3<double> dir = (target - from) * (1.0 / length);
            double end = length * maxFraction;
            double horizontalLength = std::hypot(target(0) - from(0), target(1) - from(1));
            if (horizontalLength > 0) {
                end = std::min(end, length * (1.0 - groundMargin / horizontalLength));
            }
            for (double distance = firstStep; distance < end; distance = std::max(distance * stepGrowth, distance + firstStep)) {
                cglib::vec3<double> pos = from + dir * distance;
                if (pos(2) > zTop) {
                    if (dir(2) >= 0) {
                        return false; // above every summit and climbing
                    }
                    continue;
                }
                if (groundAt(pos(0), pos(1), ground) && pos(2) < ground) {
                    return true;
                }
            }
            return false;
        }
    };

}

#endif

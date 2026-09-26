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
    };

}

#endif

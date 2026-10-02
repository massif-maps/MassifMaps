/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_LABELPLACEMENTFOLLOWUP_H_
#define _MASSIF_LABELPLACEMENTFOLLOWUP_H_

#include <cglib/mat.h>

#include <cmath>
#include <cstddef>

namespace massif {

    /**
     * Whether labels can be placed for this view. A 0x0 surface makes it NaN (ViewState::calculatePerspMat
     * divides by the height), and NaN != NaN read as a camera moving on every pass.
     */
    inline bool isLabelPlacementViewValid(const cglib::mat4x4<double>& modelviewProjectionMat) {
        for (std::size_t i = 0; i < 4; i++) {
            for (std::size_t j = 0; j < 4; j++) {
                if (!std::isfinite(modelviewProjectionMat(i, j))) {
                    return false;
                }
            }
        }
        return true;
    }

    struct LabelPlacementFollowUp {
        bool redraw;
        bool continuation;
    };

    /**
     * What a placement pass owes once done. A pass with no label anywhere owes nothing: labels that arrive
     * schedule their own pass (MapRenderer::vtLabelsChanged).
     */
    inline LabelPlacementFollowUp labelPlacementFollowUp(bool placedAny, bool cycleFinished, bool viewMoved) {
        if (!placedAny) {
            return { false, false };
        }
        return { true, !cycleFinished || viewMoved };
    }

}

#endif

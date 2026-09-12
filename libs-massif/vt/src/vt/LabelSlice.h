/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELSLICE_H_
#define _MASSIF_VT_LABELSLICE_H_

#include <cstddef>

namespace massif::vt {

    /** Labels a rationed pass examines before the deadline may stop it. */
    inline constexpr std::size_t MIN_SLICE_LABELS = 32;

    /**
     * Whether a rationed placement pass that resumed at `cursor` may stop at `index`. Free of the
     * culler so the host tests can reach it; see docs/internals/rendering/06-labels.mdx.
     *
     * The floor is the point. One deadline is shared by every layer in a pass, so a layer whose turn
     * comes after it is spent enters with the deadline already gone - and stopping on the first
     * iteration leaves the cursor exactly where it was. A cursor that does not move is a layer whose
     * labels are never placed again: measured on the Crosscall, two layers of three sat at 2048 of
     * 4075 and 96 of 172 for as long as the map was panned, so everything past those points kept
     * whatever visibility it had and no newly loaded tile ever got a label.
     *
     * The 32-label mask is only there to keep the clock read off most iterations, which is why it
     * alone could not carry this: every stuck cursor was a multiple of 32.
     */
    inline bool labelSliceMayStop(std::size_t index, std::size_t cursor, std::size_t minLabels) {
        return index - cursor >= minLabels && (index & 0x1f) == 0;
    }

}

#endif

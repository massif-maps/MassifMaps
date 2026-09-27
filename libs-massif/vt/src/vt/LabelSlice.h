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
     * Whether a rationed pass that resumed at `cursor` may stop at `index`. The floor guarantees the
     * cursor moves even when the shared deadline is already spent. See docs/internals/rendering/06-labels.mdx.
     */
    inline bool labelSliceMayStop(std::size_t index, std::size_t cursor, std::size_t minLabels) {
        return index - cursor >= minLabels && (index & 0x1f) == 0;
    }

}

#endif

/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FINESTDRAWNLEVEL_H_
#define _MASSIF_FINESTDRAWNLEVEL_H_

#include <algorithm>
#include <cstdint>

namespace massif {

    /**
     * The finest level in [coarsest, finest] whose elevation texture was drawn after `since` (an LRU stamp);
     * -1 when none. stampAt(zoom, stamp) answers false when no texture of that level holds the point.
     */
    template <typename StampAt>
    inline int finestDrawnLevel(int finest, int coarsest, std::uint64_t since, const StampAt& stampAt) {
        for (int zoom = finest; zoom >= std::max(0, coarsest); zoom--) {
            std::uint64_t stamp = 0;
            if (stampAt(zoom, stamp) && stamp > since) {
                return zoom;
            }
        }
        return -1;
    }

}

#endif

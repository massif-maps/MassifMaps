/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DRAPEEVICTION_H_
#define _MASSIF_DRAPEEVICTION_H_

namespace massif {

    /**
     * What the drape cache may evict over budget. A mask whose colour drape is still cached is kept:
     * the owner re-bakes the whole tile to restore it. See docs/internals/rendering/04-terrain.md.
     */
    struct DrapeEviction {
        /** stack 0 is the colour drape, above it the masks; colourCached = stack 0 of the same tile is cached. */
        static bool isEvictable(int stack, bool used, bool colourCached) {
            if (used) {
                return false;
            }
            return stack == 0 || !colourCached;
        }
    };

}

#endif

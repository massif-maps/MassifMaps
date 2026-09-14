/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DRAPEEVICTION_H_
#define _MASSIF_DRAPEEVICTION_H_

namespace massif {

    /**
     * What the drape cache may throw away when it is over budget.
     *
     * A coverage mask is not a tile of its own: it is part of the drape it was baked beside, and the
     * owner re-bakes the WHOLE tile to bring a missing mask back. So evicting a mask whose colour
     * drape is still cached buys a megabyte and spends a full bake to undo it, every frame, for
     * ever. Measured on the Crosscall: 104 MB cached against a 96 MB budget, ~16 evictions a second
     * of which 9 were masks, and 23 tiles re-baked a frame with nothing on screen changing.
     *
     * A mask whose colour drape is gone is a different thing - an orphan, and the first to go.
     *
     * Free of the renderer and of GL on purpose, so it is testable on the host. See
     * docs/internals/rendering/04-terrain.md.
     */
    struct DrapeEviction {
        /**
         * stack 0 is the colour drape, above it the coverage masks. 'used' is "read this frame",
         * which the cache already refuses to evict. 'colourCached' is whether stack 0 of the SAME
         * tile is still in the cache.
         */
        static bool isEvictable(int stack, bool used, bool colourCached) {
            if (used) {
                return false;
            }
            return stack == 0 || !colourCached;
        }
    };

}

#endif

/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TESSELATIONBOUNDS_H_
#define _MASSIF_TESSELATIONBOUNDS_H_

#include <cglib/bbox.h>

namespace massif {

    /**
     * The extent the terrain surface refinement covers, in tile-local units.
     * See TerrainTileTransformer::TerrainVertexTransformer::tesselateTriangle.
     */
    struct TesselationBounds {
        /**
         * How far past its border a tile still refines, in tile widths: only a drape bake sampling a little
         * past it needs this. Kept small because it costs the square (1/4 would refine 2.25x the area).
         */
        static constexpr float MARGIN = 1.0f / 32.0f;

        static cglib::bbox2<float> box() {
            return cglib::bbox2<float>(cglib::vec2<float>(-MARGIN, -MARGIN),
                                       cglib::vec2<float>(1 + MARGIN, 1 + MARGIN));
        }

        /**
         * Whether a triangle with this bounding box is worth refining. At overzoom a source tile's buffer
         * reaches whole tile widths past the border at the target's split threshold; past the border
         * everything is clipped per fragment anyway.
         */
        static bool refines(const cglib::bbox2<float>& bounds) {
            return box().inside(bounds); // cglib: inside(bbox) is intersects, not containment
        }
    };

}

#endif

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
     * The extent the terrain surface refinement covers, in TILE-LOCAL units. Free of the
     * transformer on purpose, so the boundary is testable on the host. See
     * TerrainTileTransformer::TerrainVertexTransformer::tesselateTriangle.
     */
    struct TesselationBounds {
        /**
         * How far past its own border a tile still refines geometry, in tile widths. A triangle
         * whose vertices are outside but whose body covers the tile is caught by the overlap test
         * itself, so this only has to cover a drape bake sampling a little past the border: 1/32 of
         * a tile is about 2 m at z19. It costs the SQUARE - at 1/4 a tile refines 2.25x its own
         * area - which is why it is not simply generous.
         */
        static constexpr float MARGIN = 1.0f / 32.0f;

        static cglib::bbox2<float> box() {
            return cglib::bbox2<float>(cglib::vec2<float>(-MARGIN, -MARGIN),
                                       cglib::vec2<float>(1 + MARGIN, 1 + MARGIN));
        }

        /**
         * Whether a triangle with this bounding box is worth refining.
         *
         * A source tile keeps a buffer of geometry around its own data, and at OVERZOOM that buffer
         * scales with everything else: a z14 source drawn into a z19 target reaches past the border
         * by whole tile widths, while the split threshold is the z19 one. The polygon gate upstream
         * is an INTERSECTS test, so one triangle touching the tile was refined across its whole
         * extent - measured over Paris at z19, an edge of 145 m against a 2.4 m threshold, which is
         * 4096 triangles out of one. Everything past the border is clipped per fragment anyway.
         */
        static bool refines(const cglib::bbox2<float>& bounds) {
            return box().inside(bounds); // cglib: inside(bbox) is INTERSECTS, not containment
        }
    };

}

#endif

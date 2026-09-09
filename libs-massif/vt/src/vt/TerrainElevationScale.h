/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_TERRAINELEVATIONSCALE_H_
#define _MASSIF_VT_TERRAINELEVATIONSCALE_H_

#include "TileId.h"
#include "TileTransformer.h"

#include <cglib/mat.h>

namespace massif::vt {
    /**
     * Metres to the z units of a vertex frame on a SPHERE - what uElevationScale.x carries there.
     *
     * A height on a sphere is radial, so it is one number for the whole tile. calculateHeight
     * answers in the tile's OWN frame (coordScale 1), while a draw's vertices sit in that frame, in
     * a coordScale'd one or in internal coordinates: the ratio of the two frames' z scales converts
     * it, and is 1 when they are the same frame. Missing it made 4 km of relief 0.13 tile-local
     * units instead of 5.1 at zoom 13 - a flat globe. docs/internals/rendering/18-globe.md.
     */
    inline double sphericalMetersToFrame(const TileTransformer& transformer, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix) {
        double frameScaleZ = (vertexFrameMatrix(2, 2) != 0 ? vertexFrameMatrix(2, 2) : 1.0);
        double tileScaleZ = transformer.calculateTileMatrix(tileId, 1.0f)(2, 2);
        double localPerMeter = transformer.createTileVertexTransformer(tileId)->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1.0f);
        return localPerMeter * tileScaleZ / frameScaleZ;
    }
}

#endif

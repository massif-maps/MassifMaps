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
     * Metres to the z units of a vertex frame on a sphere (uElevationScale.x there), one number per tile.
     * calculateHeight answers in the tile's own frame (coordScale 1); the z-scale ratio converts it to
     * the draw's frame. See docs/internals/rendering/18-globe.md.
     */
    inline double sphericalMetersToFrame(const TileTransformer& transformer, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix) {
        double frameScaleZ = (vertexFrameMatrix(2, 2) != 0 ? vertexFrameMatrix(2, 2) : 1.0);
        double tileScaleZ = transformer.calculateTileMatrix(tileId, 1.0f)(2, 2);
        double localPerMeter = transformer.createTileVertexTransformer(tileId)->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1.0f);
        return localPerMeter * tileScaleZ / frameScaleZ;
    }
}

#endif

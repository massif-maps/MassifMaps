/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILEBITMAP_H_
#define _MASSIF_TILEBITMAP_H_

#include <memory>

namespace massif {
    class Bitmap;
    class TileData;

    /**
     * Decodes the tile's encoded file, or wraps the raw RGBA8 of a raw-pixel TileData. Every consumer turning a tile
     * into a bitmap must go through here: CreateFromCompressed would read a raw tile as a corrupt PNG.
     * @param tileData The tile, or null.
     * @return The bitmap, or null when there is no data or it could not be decoded.
     */
    std::shared_ptr<Bitmap> DecodeTileBitmap(const std::shared_ptr<TileData>& tileData);

}

#endif

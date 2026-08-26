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
     * The tile's pixels, whichever way the source produced them.
     *
     * Decodes the encoded file a normal source hands over, or wraps the raw RGBA8 of a source that
     * produced pixels directly (TileData's raw-pixel constructor). Every consumer that turns a
     * tile into a bitmap goes through here - one that called CreateFromCompressed itself would
     * read a raw tile as a corrupt PNG and draw nothing.
     *
     * @param tileData The tile, or null.
     * @return The bitmap, or null when there is no data or it could not be decoded.
     */
    std::shared_ptr<Bitmap> DecodeTileBitmap(const std::shared_ptr<TileData>& tileData);

}

#endif

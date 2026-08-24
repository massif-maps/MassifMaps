/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILEDOWNLOADINFO_H_
#define _MASSIF_TILEDOWNLOADINFO_H_

#include "core/MapTile.h"

#include <string>

namespace massif {

    /**
     * What a tile download reports, as one object.
     *
     * A listener has four callbacks with four different arguments; a facade event carries a payload,
     * and one payload class over the four is what lets a binding subscribe to "the download" rather
     * than to each of them. Only the fields the event in question fills are meaningful - `progress`
     * on download.progress, `tile` on download.failed.
     */
    class TileDownloadInfo {
    public:
        /**
         * Constructs a TileDownloadInfo object from a tile count, a progress and a tile.
         * @param tileCount The number of tiles the download will fetch, or -1 when not yet known.
         * @param progress The progress of the download, from 0 to 100.
         * @param tile The tile the event concerns. Meaningful for a failure.
         */
        TileDownloadInfo(int tileCount, float progress, const MapTile& tile);
        virtual ~TileDownloadInfo();

        /**
         * Returns the number of tiles the download will fetch.
         * @return The tile count, or -1 when it is not known yet.
         */
        int getTileCount() const;

        /**
         * Returns the progress of the download.
         * @return The progress, from 0 to 100.
         */
        float getProgress() const;

        /**
         * Returns the tile the event concerns, which is only meaningful for a failure.
         * @return The tile.
         */
        const MapTile& getTile() const;

        /**
         * Creates a string representation of this object, useful for logging.
         * @return The string representation of this object.
         */
        std::string toString() const;

    private:
        int _tileCount;
        float _progress;
        MapTile _tile;
    };

}

#endif

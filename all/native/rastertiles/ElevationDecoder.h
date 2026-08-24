/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ELEVATIONDECODER_H_
#define _MASSIF_ELEVATIONDECODER_H_

#include <array>
#include <memory>
#include <string>

namespace massif {
    class TileData;
    class TileDataSource;

    /**
     * Abstract base class for raster elevation decoders.
     */
    class ElevationDecoder {
    public:
    /**
         * Constructs an ElevationDecoder.
         */
        ElevationDecoder();
        virtual ~ElevationDecoder();

        virtual std::array<float, 4> getVectorTileScales() const = 0;
        virtual std::array<double, 4> getColorComponentCoefficients() const = 0;
        virtual float getMinimumHeightScale() const = 0;

        /**
         * The meta data key naming the DEM pixel encoding, "mapbox" or "terrarium".
         */
        static const std::string ENCODING_KEY;

        /**
         * Resolves the decoder for one elevation tile: the tile's own "dem_encoding" first - two
         * sources of different encodings can sit behind one OrderedTileDataSource, and only the
         * tile knows which answered - then the source's, then 'preferred', then MapBox.
         * Both arguments may be null. The returned decoders are shared singletons.
         * @param tileData The loaded elevation tile, or null.
         * @param dataSource The elevation data source, or null.
         * @param preferred The decoder the caller was configured with, or null.
         * @return The decoder to decode this tile with. Never null.
         */
        static std::shared_ptr<ElevationDecoder> Resolve(const std::shared_ptr<TileData>& tileData,
                                                         const std::shared_ptr<TileDataSource>& dataSource,
                                                         const std::shared_ptr<ElevationDecoder>& preferred);
    };

}

#endif

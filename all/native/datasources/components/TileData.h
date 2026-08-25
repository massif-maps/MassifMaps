/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILEDATA_H_
#define _MASSIF_TILEDATA_H_

#include "core/Variant.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <vector>
#include <map>
#include <string>

namespace massif {
    class BinaryData;

    /**
     * A wrapper class for tile data.
     */
    class TileData {
    public:
        /**
         * Constructs a TileData object from a data blob.
         * @param data The source tile data.
         */
        TileData(const std::shared_ptr<BinaryData>& data);
        /**
         * Constructs a TileData object from RAW, already decoded pixels.
         *
         * For a source that produces pixels rather than a file - GDAL, a procedural tile, a
         * decoder of a format the SDK does not know. The alternative is to encode a PNG the SDK
         * then immediately decodes again, which is two codecs and three copies per tile.
         *
         * The layout is RGBA8, premultiplied, tightly packed: exactly width * height * 4 bytes,
         * no row padding. One format on purpose - a second one would put a switch in every
         * consumer for a case none of them has.
         *
         * Raster tiles only, and NOT persistently cached: the bytes carry no format, so a cache
         * that stored them would read them back as a compressed file. See
         * PersistentCacheTileDataSource::store.
         *
         * @param pixels width * height * 4 bytes of premultiplied RGBA.
         * @param width The tile width in pixels.
         * @param height The tile height in pixels.
         */
        TileData(const std::shared_ptr<BinaryData>& pixels, int width, int height);
        virtual ~TileData();
        
        /**
         * Returns the maximum age of the tile data, tile data will expire after that point.
         * @return Tile data maximum age in milliseconds, or -1 if the data does not expire.
         */
        long long getMaxAge() const;
        /**
         * Sets the maximum age of tile data, tile data will expire after that point.
         * @param maxAge Tile data maximum age in milliseconds, or -1 if the data does not expire.
         */
        void setMaxAge(long long maxAge);
        
        /**
         * Returns true if the tile should be replaced with parent tile.
         * @return True if the tile should be replaced with parent. False otherwise.
         */
        bool isReplaceWithParent() const;
        /**
         * Set the parent replacement flag.
         * @param flag True when the tile should be replaced with the parent, false otherwise.
         */
        void setReplaceWithParent(bool flag);

        /**
         * Returns true if the tile data source marked this as over zoom.
         * @return True if the tile should not be drawn. False otherwise.
         */
        bool isOverZoom() const;
        /**
         * Set the parent overzoom flag.
         * @return True if the tile should not be drawn. False otherwise.
         */
        void setIsOverZoom(bool flag);
        
        /**
         * Returns tile data as binary data.
         * @return Tile data as binary data.
         */
        const std::shared_ptr<BinaryData>& getData() const;

        /**
         * Returns true when getData() holds raw RGBA8 pixels rather than an encoded file.
         * A consumer that turns tiles into bitmaps has to check this before decoding.
         * @return True if the data is raw pixels.
         */
        bool isRawPixels() const;
        /**
         * The pixel width, or 0 when the data is an encoded file.
         * @return The width in pixels.
         */
        int getWidth() const;
        /**
         * The pixel height, or 0 when the data is an encoded file.
         * @return The height in pixels.
         */
        int getHeight() const;
        
        /**
         * Returns the meta data map carried by this tile - the meta data of the data source that
         * produced it. May be null when the source carries none.
         * The map is immutable and shared by every tile of that source: attaching it costs one
         * atomic increment, and setMetaDataElement copies before writing.
         * @return The meta data map, or null.
         */
        std::shared_ptr<const std::map<std::string, Variant> > getMetaData() const;
        /**
         * Attaches a meta data map to this tile, replacing any previous one.
         * @param metaData The meta data map. May be null.
         */
        void setMetaData(const std::shared_ptr<const std::map<std::string, Variant> >& metaData);

        /**
         * Returns true if the specified key exists in the tile meta data map.
         * @param key The key to check.
         * @return True if the meta data element exists.
         */
        bool containsMetaDataKey(const std::string& key) const;
        /**
         * Returns a meta data element corresponding to the key. If no value is found null variant is returned.
         * @param key The key to use.
         * @return The value corresponding to the key, or an empty variant.
         */
        Variant getMetaDataElement(const std::string& key) const;
        /**
         * Adds a new key-value pair to the meta data map, copying it first - the map is shared with
         * every other tile of the same source.
         * @param key The new key.
         * @param element The new value.
         */
        void setMetaDataElement(const std::string& key, const Variant& element);

    private:
        const std::shared_ptr<BinaryData> _data;
        // 0 when _data is an encoded file, which is what isRawPixels() reads.
        const int _width;
        const int _height;
        std::shared_ptr<std::chrono::steady_clock::time_point> _expirationTime;
        bool _replaceWithParent;
        bool _overzoom;
        // using > > without the space breaks swig
        std::shared_ptr<const std::map<std::string, Variant> > _metaData;
        mutable std::mutex _mutex;
    };

}

#endif

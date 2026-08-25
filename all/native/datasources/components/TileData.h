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
        std::shared_ptr<std::chrono::steady_clock::time_point> _expirationTime;
        bool _replaceWithParent;
        bool _overzoom;
        // using > > without the space breaks swig
        std::shared_ptr<const std::map<std::string, Variant> > _metaData;
        mutable std::mutex _mutex;
    };

}

#endif

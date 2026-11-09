/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_MBVTFEATUREDECODER_H_
#define _MASSIF_MAPNIKVT_MBVTFEATUREDECODER_H_

#include "LayerFeatureDecoder.h"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <vector>
#include <map>
#include <set>

namespace vector_tile {
    class Tile;
}

namespace massif::mvt {
    class Logger;
    
    // The parsed tiles of the last few byte strings, so the layers of a composite that decode one
    // tile each pay for the protobuf parse once. Callers asking at once wait for the first parse.
    class MBVTTileCache {
    public:
        using ParseFunc = std::function<std::shared_ptr<const vector_tile::Tile>()>;

        explicit MBVTTileCache(std::size_t capacity = 8);

        // `parse` runs for the first caller only; what it returns (null included) is kept.
        std::shared_ptr<const vector_tile::Tile> get(const std::vector<unsigned char>& data, const ParseFunc& parse);

    private:
        struct Entry {
            std::uint64_t hash = 0;
            std::size_t size = 0;
            std::mutex mutex;
            bool ready = false;
            std::shared_ptr<const vector_tile::Tile> tile;
        };

        const std::size_t _capacity;
        std::mutex _mutex;
        std::list<std::shared_ptr<Entry>> _entries;
    };

    class MBVTFeatureDecoder : public LayerFeatureDecoder {
    public:
        explicit MBVTFeatureDecoder(const std::vector<unsigned char>& data, std::shared_ptr<Logger> logger);
        explicit MBVTFeatureDecoder(std::shared_ptr<const vector_tile::Tile> tile, std::shared_ptr<Logger> logger);

        // What the constructor from bytes parses: the bytes inflated if they are, else read as they are.
        static std::shared_ptr<const vector_tile::Tile> parseTile(const std::vector<unsigned char>& data);

        virtual std::vector<std::string> getLayerNames() const override;

        virtual bool hasLayer(const std::string& name) const override;

        virtual std::shared_ptr<FeatureIterator> createLayerFeatureIterator(const std::string& name, const std::set<std::string>* fields, bool clip = true) const override;

        virtual bool findFeature(long long localId, std::string& layerName, Feature& feature) const override;

        virtual bool mayHaveFieldValue(const std::string& layerName, const std::string& field, const Value& value) const override;

    protected:
        virtual void invalidateGeometryCache() override;

    private:
        class MBVTFeatureIterator;

        const std::shared_ptr<Logger> _logger;

        std::shared_ptr<const vector_tile::Tile> _tile;
        std::map<std::string, int> _layerMap;

        mutable std::pair<std::string, std::shared_ptr<GeometryCache>> _layerGeometryCache;
        mutable std::map<std::string, std::shared_ptr<FeatureDataCache<std::vector<int>>>> _layerFeatureDataCaches;
        mutable std::map<int, std::shared_ptr<const std::vector<cglib::bbox2<float>>>> _layerFeatureBounds;
        mutable std::map<std::pair<int, std::string>, std::vector<Value>> _layerFieldValues;
        mutable std::mutex _layerCacheMutex;
    };
}

#endif

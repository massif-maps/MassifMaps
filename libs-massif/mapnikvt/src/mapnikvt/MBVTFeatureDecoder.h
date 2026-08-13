/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_MBVTFEATUREDECODER_H_
#define _CARTO_MAPNIKVT_MBVTFEATUREDECODER_H_

#include "LayerFeatureDecoder.h"

#include <memory>
#include <mutex>
#include <vector>
#include <map>
#include <set>

namespace vector_tile {
    class Tile;
}

namespace carto::mvt {
    class Logger;
    
    class MBVTFeatureDecoder : public LayerFeatureDecoder {
    public:
        explicit MBVTFeatureDecoder(const std::vector<unsigned char>& data, std::shared_ptr<Logger> logger);

        virtual std::vector<std::string> getLayerNames() const override;

        virtual bool hasLayer(const std::string& name) const override;

        virtual std::shared_ptr<FeatureIterator> createLayerFeatureIterator(const std::string& name, const std::set<std::string>* fields) const override;

        virtual bool findFeature(long long localId, std::string& layerName, Feature& feature) const override;

    protected:
        virtual void invalidateGeometryCache() override;

    private:
        class MBVTFeatureIterator;

        const std::shared_ptr<Logger> _logger;

        std::shared_ptr<vector_tile::Tile> _tile;
        std::map<std::string, int> _layerMap;

        mutable std::pair<std::string, std::shared_ptr<GeometryCache>> _layerGeometryCache;
        mutable std::pair<std::string, std::shared_ptr<FeatureDataCache<std::vector<int>>>> _layerFeatureDataCache;
        mutable std::mutex _layerCacheMutex;
    };
}

#endif

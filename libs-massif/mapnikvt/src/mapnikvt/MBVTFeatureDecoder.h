/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_MBVTFEATUREDECODER_H_
#define _CARTO_MAPNIKVT_MBVTFEATUREDECODER_H_

#include "FeatureDecoder.h"

#include <memory>
#include <mutex>
#include <vector>
#include <map>
#include <set>

#include <cglib/bbox.h>
#include <cglib/mat.h>

namespace vector_tile {
    class Tile;
}

namespace carto::mvt {
    class Logger;
    
    class MBVTFeatureDecoder : public FeatureDecoder {
    public:
        explicit MBVTFeatureDecoder(const std::vector<unsigned char>& data, std::shared_ptr<Logger> logger);

        void setTransform(const cglib::mat3x3<float>& transform);
        void setClipBox(const cglib::bbox2<float>& clipBox);
        void setFeatureIdOverride(bool featureIdOverride, long long tileIdOffset = 0);

        std::vector<std::string> getLayerNames() const;

        std::shared_ptr<FeatureIterator> createLayerFeatureIterator(const std::string& name, const std::set<std::string>* fields) const;

        bool findFeature(long long localId, std::string& layerName, Feature& feature) const;

    private:
        class MBVTFeatureIterator;

        const std::shared_ptr<Logger> _logger;

        cglib::mat3x3<float> _transform = cglib::mat3x3<float>::identity();
        cglib::bbox2<float> _clipBox = cglib::bbox2<float>(cglib::vec2<float>(-0.125f, -0.125f), cglib::vec2<float>(1.125f, 1.125f));
        bool _featureIdOverride = false;
        long long _tileIdOffset = 0;
        std::shared_ptr<vector_tile::Tile> _tile;
        std::map<std::string, int> _layerMap;

        mutable std::pair<std::string, std::shared_ptr<GeometryCache>> _layerGeometryCache;
        mutable std::pair<std::string, std::shared_ptr<FeatureDataCache<std::vector<int>>>> _layerFeatureDataCache;
        mutable std::mutex _layerCacheMutex;
    };
}

#endif

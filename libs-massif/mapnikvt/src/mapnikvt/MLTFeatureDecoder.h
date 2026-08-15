/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_MLTFEATUREDECODER_H_
#define _MASSIF_MAPNIKVT_MLTFEATUREDECODER_H_

#include "LayerFeatureDecoder.h"

#include <memory>
#include <mutex>
#include <vector>
#include <map>
#include <set>

namespace mlt {
    class MapLibreTile;
}

namespace massif::mvt {
    class Logger;

    /**
     * Decoder for MapLibre Tiles, the columnar successor to MVT. Wraps maplibre-tile-spec's C++
     * decoder (libs-external/mlt) and presents its features the same way MBVTFeatureDecoder
     * presents MVT ones. Note that unlike MVT, an MLT tile is decoded whole - the format has no
     * per-layer lazy path.
     */
    class MLTFeatureDecoder : public LayerFeatureDecoder {
    public:
        explicit MLTFeatureDecoder(const std::vector<unsigned char>& data, std::shared_ptr<Logger> logger);
        ~MLTFeatureDecoder();

        /**
         * Tests whether uncompressed tile data is MLT rather than MVT. See the format section of
         * docs/rendering/02-tiles.md for what this recognises and what it was measured against.
         */
        static bool isTileData(const unsigned char* data, std::size_t size);

        virtual std::vector<std::string> getLayerNames() const override;

        virtual bool hasLayer(const std::string& name) const override;

        virtual std::shared_ptr<FeatureIterator> createLayerFeatureIterator(const std::string& name, const std::set<std::string>* fields) const override;

        virtual bool findFeature(long long localId, std::string& layerName, Feature& feature) const override;

    protected:
        virtual void invalidateGeometryCache() override;

    private:
        class MLTFeatureIterator;

        const std::shared_ptr<Logger> _logger;

        std::shared_ptr<const mlt::MapLibreTile> _tile;
        std::map<std::string, int> _layerMap;
        std::vector<std::vector<std::string>> _layerKeys; // property keys per layer, sorted for a stable order

        mutable std::pair<std::string, std::shared_ptr<GeometryCache>> _layerGeometryCache;
        mutable std::mutex _layerCacheMutex;
    };
}

#endif

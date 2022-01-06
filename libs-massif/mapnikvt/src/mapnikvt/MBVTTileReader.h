/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_MBVTTILEREADER_H_
#define _CARTO_MAPNIKVT_MBVTTILEREADER_H_

#include "TileReader.h"
#include "MBVTFeatureDecoder.h"
#include "Map.h"

namespace carto::mvt {
    class MBVTTileReader : public TileReader {
    public:
        explicit MBVTTileReader(std::shared_ptr<const Map> map, std::shared_ptr<const vt::TileTransformer> transformer, const SymbolizerContext& symbolizerContext, const MBVTFeatureDecoder& featureDecoder, std::shared_ptr<Logger> logger) : TileReader(std::move(map), std::move(transformer), symbolizerContext, std::move(logger)), _featureDecoder(featureDecoder) { }

        void setLayerNameOverride(const std::string& name);

    protected:
        virtual std::shared_ptr<vt::TileBackground> createTileBackground(const vt::TileId& tileId) const override;
        
        virtual std::shared_ptr<FeatureDecoder::FeatureIterator> createFeatureIterator(const std::shared_ptr<const Layer>& layer, const std::set<std::string>* fields) const override;

        const MBVTFeatureDecoder& _featureDecoder;
        std::string _layerNameOverride;
    };
}

#endif

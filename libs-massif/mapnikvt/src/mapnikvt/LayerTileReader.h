/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_LAYERTILEREADER_H_
#define _MASSIF_MAPNIKVT_LAYERTILEREADER_H_

#include "TileReader.h"
#include "LayerFeatureDecoder.h"
#include "Map.h"

namespace massif::mvt {
    class LayerTileReader : public TileReader {
    public:
        explicit LayerTileReader(std::shared_ptr<const Map> map, std::shared_ptr<const vt::TileTransformer> transformer, const SymbolizerContext& symbolizerContext, const LayerFeatureDecoder& featureDecoder, std::shared_ptr<Logger> logger) : TileReader(std::move(map), std::move(transformer), symbolizerContext, std::move(logger)), _featureDecoder(featureDecoder) { }

        void setLayerNameOverride(const std::string& name);

    protected:
        virtual std::shared_ptr<vt::TileBackground> createTileBackground(const vt::TileId& tileId, const ExpressionContext& exprContext) const override;
        
        virtual std::shared_ptr<FeatureDecoder::FeatureIterator> createFeatureIterator(const std::shared_ptr<const Layer>& layer, const std::set<std::string>* fields) const override;

        virtual bool hasLayer(const std::shared_ptr<const Layer>& layer) const override;

        std::string resolveLayerName(const std::shared_ptr<const Layer>& layer) const;

        const LayerFeatureDecoder& _featureDecoder;
        std::string _layerNameOverride;
    };
}

#endif

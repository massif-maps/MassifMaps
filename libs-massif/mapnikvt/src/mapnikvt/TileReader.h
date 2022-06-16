/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TILEREADER_H_
#define _CARTO_MAPNIKVT_TILEREADER_H_

#include "FeatureDecoder.h"
#include "ExpressionContext.h"
#include "SymbolizerContext.h"
#include "Logger.h"
#include "vt/Tile.h"
#include "vt/TileTransformer.h"
#include "vt/TileLayerBuilder.h"

#include <memory>
#include <string>
#include <set>
#include <vector>

namespace carto::mvt {
    class Filter;
    class Rule;
    class Symbolizer;
    class Style;
    class Layer;
    class Map;

    class TileReader {
    public:
        virtual ~TileReader() = default;

        virtual std::shared_ptr<vt::Tile> readTile(const vt::TileId& tileId) const;

    protected:
        explicit TileReader(std::shared_ptr<const Map> map, std::shared_ptr<const vt::TileTransformer> transformer, const SymbolizerContext& symbolizerContext, std::shared_ptr<Logger> logger);

        void processLayer(const std::shared_ptr<const Layer>& layer, const std::shared_ptr<const Style>& style, ExpressionContext& exprContext, vt::TileLayerBuilder& layerBuilder) const;

        std::vector<std::shared_ptr<const Rule>> preFilterStyleRules(const std::shared_ptr<const Style>& style, ExpressionContext& exprContext) const;
        std::vector<std::shared_ptr<const Symbolizer>> findFeatureSymbolizers(const std::shared_ptr<const Style>& style, const std::vector<std::shared_ptr<const Rule>>& rules, ExpressionContext& exprContext) const;

        virtual std::shared_ptr<vt::TileBackground> createTileBackground(const vt::TileId& tileId, const ExpressionContext& exprContext) const = 0;

        virtual std::shared_ptr<FeatureDecoder::FeatureIterator> createFeatureIterator(const std::shared_ptr<const Layer>& layer, const std::set<std::string>* fields) const = 0;

        const std::shared_ptr<const Map> _map;
        const std::shared_ptr<const vt::TileTransformer> _transformer;
        const SymbolizerContext& _symbolizerContext;
        const std::shared_ptr<Logger> _logger;
        const std::shared_ptr<const Filter> _trueFilter;
    };
}

#endif

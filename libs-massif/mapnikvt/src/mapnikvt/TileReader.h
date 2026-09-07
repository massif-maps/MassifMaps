/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_TILEREADER_H_
#define _MASSIF_MAPNIKVT_TILEREADER_H_

#include "FeatureDecoder.h"
#include "ExpressionContext.h"
#include "SelectionParameter.h"
#include "Symbolizer.h"
#include "SymbolizerContext.h"
#include "Logger.h"
#include "vt/Tile.h"
#include "vt/TileTransformer.h"
#include "vt/TileLayerBuilder.h"

#include <memory>
#include <string>
#include <map>
#include <unordered_map>
#include <set>
#include <vector>

namespace massif::mvt {
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

        void processLayer(const std::shared_ptr<const Layer>& layer, const std::shared_ptr<const Style>& style, const std::vector<std::shared_ptr<const Rule>>& rules, ExpressionContext& exprContext, std::uint64_t selectionStateKey, vt::TileLayerBuilder& layerBuilder) const;

        // One processor drawing the feature once but registering BOTH styles the selecting parameter
        // can give it, so a change to the parameter is a repaint - see SelectionParameter.
        std::shared_ptr<Symbolizer::FeatureProcessor> createSelectionFeatureProcessor(const std::shared_ptr<const Symbolizer>& symbolizer, const SelectionParameter& selectionParameter, std::uint64_t stateKey, ExpressionContext& exprContext) const;

        std::vector<std::shared_ptr<const Rule>> preFilterStyleRules(const std::shared_ptr<const Style>& style, ExpressionContext& exprContext) const;
        std::vector<std::shared_ptr<const Symbolizer>> findFeatureSymbolizers(const std::shared_ptr<const Style>& style, const std::vector<std::shared_ptr<const Rule>>& rules, ExpressionContext& exprContext) const;

        virtual std::shared_ptr<vt::TileBackground> createTileBackground(const vt::TileId& tileId, const ExpressionContext& exprContext) const = 0;

        virtual std::shared_ptr<FeatureDecoder::FeatureIterator> createFeatureIterator(const std::shared_ptr<const Layer>& layer, const std::set<std::string>* fields) const = 0;

        // The extrusion anchor pass needs the layer's features UNCLIPPED - the parts of a building
        // this tile does not draw still decide where the ones it draws read their ground. A source
        // that cannot hand those over answers with nothing and every footprint keeps its own
        // centroid, which is the behaviour before the pass existed.
        virtual std::shared_ptr<FeatureDecoder::FeatureIterator> createUnclippedFeatureIterator(const std::shared_ptr<const Layer>& layer, const std::set<std::string>* fields) const { return std::shared_ptr<FeatureDecoder::FeatureIterator>(); }

        // The box this tile's data was cut at, in feature coordinates; the unit square unless the
        // tile is overzoomed from an ancestor.
        virtual cglib::bbox2<float> getSourceBox() const { return cglib::bbox2<float>(cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 1)); }

        // Whether the tile carries this layer at all - readTile skips everything it can for a layer
        // it does not. Sources that do not address features by layer answer yes.
        virtual bool hasLayer(const std::shared_ptr<const Layer>& layer) const { return true; }

        const std::shared_ptr<const Map> _map;
        const std::shared_ptr<const vt::TileTransformer> _transformer;
        const SymbolizerContext& _symbolizerContext;
        const std::shared_ptr<Logger> _logger;
        const std::shared_ptr<const Filter> _trueFilter;
        // Per LAYER for the life of this reader, which is one tile: the anchor pass reads the whole
        // layer, and a layer two extruding styles draw would otherwise read it twice.
        mutable std::map<const Layer*, std::shared_ptr<const std::unordered_map<long long, cglib::vec2<float>>>> _extrusionAnchors;
    };
}

#endif

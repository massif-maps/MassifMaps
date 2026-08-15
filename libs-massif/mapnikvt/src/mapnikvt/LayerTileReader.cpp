#include "LayerTileReader.h"
#include "Layer.h"

namespace massif::mvt {
    void LayerTileReader::setLayerNameOverride(const std::string& name) {
        _layerNameOverride = name;
    }

    std::shared_ptr<vt::TileBackground> LayerTileReader::createTileBackground(const vt::TileId& tileId, const ExpressionContext& exprContext) const {
        std::shared_ptr<const vt::BitmapPattern> backgroundBitmapPattern;
        if (!_map->getSettings().backgroundImage.empty()) {
            backgroundBitmapPattern = _symbolizerContext.getBitmapManager()->loadBitmapPattern(_map->getSettings().backgroundImage, 1.0f, 1.0f);
        }
        return std::make_shared<vt::TileBackground>(_map->getSettings().backgroundColor.getFunction(exprContext), backgroundBitmapPattern);
    }
    
    std::shared_ptr<FeatureDecoder::FeatureIterator> LayerTileReader::createFeatureIterator(const std::shared_ptr<const Layer>& layer, const std::set<std::string>* fields) const {
        return _featureDecoder.createLayerFeatureIterator(resolveLayerName(layer), fields);
    }

    bool LayerTileReader::hasLayer(const std::shared_ptr<const Layer>& layer) const {
        return _featureDecoder.hasLayer(resolveLayerName(layer));
    }

    std::string LayerTileReader::resolveLayerName(const std::shared_ptr<const Layer>& layer) const {
        return _layerNameOverride.empty() ? layer->getName() : _layerNameOverride;
    }
}

#include "MBVTTileReader.h"
#include "Layer.h"

namespace carto::mvt {
    void MBVTTileReader::setLayerNameOverride(const std::string& name) {
        _layerNameOverride = name;
    }

    std::shared_ptr<vt::TileBackground> MBVTTileReader::createTileBackground(const vt::TileId& tileId, const ExpressionContext& exprContext) const {
        std::shared_ptr<const vt::BitmapPattern> backgroundBitmapPattern;
        if (!_map->getSettings().backgroundImage.empty()) {
            backgroundBitmapPattern = _symbolizerContext.getBitmapManager()->loadBitmapPattern(_map->getSettings().backgroundImage, 1.0f, 1.0f);
        }
        return std::make_shared<vt::TileBackground>(_map->getSettings().backgroundColor.getFunction(exprContext), backgroundBitmapPattern);
    }
    
    std::shared_ptr<FeatureDecoder::FeatureIterator> MBVTTileReader::createFeatureIterator(const std::shared_ptr<const Layer>& layer, const std::set<std::string>* fields) const {
        return _featureDecoder.createLayerFeatureIterator(resolveLayerName(layer), fields);
    }

    bool MBVTTileReader::hasLayer(const std::shared_ptr<const Layer>& layer) const {
        return _featureDecoder.hasLayer(resolveLayerName(layer));
    }

    std::string MBVTTileReader::resolveLayerName(const std::shared_ptr<const Layer>& layer) const {
        return _layerNameOverride.empty() ? layer->getName() : _layerNameOverride;
    }
}

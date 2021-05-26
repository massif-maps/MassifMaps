#include "TorqueTileReader.h"
#include "TorqueLayer.h"

namespace carto { namespace mvt {
    std::shared_ptr<vt::TileBackground> TorqueTileReader::createTileBackground(const vt::TileId& tileId) const {
        const TorqueMap::TorqueSettings& torqueSettings = std::dynamic_pointer_cast<const TorqueMap>(_map)->getTorqueSettings();
        return std::make_shared<vt::TileBackground>(torqueSettings.clearColor, std::shared_ptr<const vt::BitmapPattern>());
    }
    
    std::shared_ptr<FeatureDecoder::FeatureIterator> TorqueTileReader::createFeatureIterator(const std::shared_ptr<const Layer>& layer, const std::set<std::string>* fields) const {
        const TorqueMap::TorqueSettings& torqueSettings = std::dynamic_pointer_cast<const TorqueMap>(_map)->getTorqueSettings();
        int frameOffset = 0;
        if (auto torqueLayer = std::dynamic_pointer_cast<const TorqueLayer>(layer)) {
            frameOffset = torqueLayer->getFrameOffset();
        }
        int frame = _frame - frameOffset;
        if (_loop) {
            if (frame >= 0 && torqueSettings.frameCount > 0) {
                frame = frame % torqueSettings.frameCount;
            }
        }

        return _featureDecoder.createFrameFeatureIterator(frame, frameOffset);
    }
} }

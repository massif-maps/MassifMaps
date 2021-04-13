#include "TorqueTileReader.h"
#include "TorqueLayer.h"
#include "TorqueFeatureDecoder.h"

namespace carto { namespace mvt {
    std::shared_ptr<vt::TileBackground> TorqueTileReader::createTileBackground(const vt::TileId& tileId) const {
        return std::make_shared<vt::TileBackground>(std::dynamic_pointer_cast<const TorqueMap>(_map)->getTorqueSettings().clearColor, std::shared_ptr<const vt::BitmapPattern>());
    }
    
    std::shared_ptr<FeatureDecoder::FeatureIterator> TorqueTileReader::createFeatureIterator(const std::shared_ptr<const Layer>& layer, const std::set<std::string>* fields) const {
        int frameOffset = 0;
        if (auto torqueLayer = std::dynamic_pointer_cast<const TorqueLayer>(layer)) {
            frameOffset = torqueLayer->getFrameOffset();
        }
        int frame = _frame - frameOffset;
        if (_loop) {
            int frameCount = std::dynamic_pointer_cast<const TorqueMap>(_map)->getTorqueSettings().frameCount;
            if (frame >= 0 && frameCount > 0) {
                frame = frame % frameCount;
            }
        }

        return _featureDecoder.createFrameFeatureIterator(frame);
    }
} }

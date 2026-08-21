#include "VectorTileClickInfo.h"
#include "geometry/MultiGeometry.h"

namespace massif {

    VectorTileClickInfo::VectorTileClickInfo(const ClickInfo& clickInfo, const MapPos& clickPos, const MapPos& featureClickPos,
                                             const std::shared_ptr<VectorTileFeature>& feature,
                                             const std::shared_ptr<Layer>& layer,
                                             const int featurePosIndex) :
        _clickInfo(clickInfo),
        _clickPos(clickPos),
        _featureClickPos(featureClickPos),
        _feature(feature),
        _layer(layer),
        _featurePosIndex(featurePosIndex)
    {
    }
    
    VectorTileClickInfo::~VectorTileClickInfo() {
    }

    ClickType::ClickType VectorTileClickInfo::getClickType() const {
        return _clickInfo.getClickType();
    }
    
    const ClickInfo& VectorTileClickInfo::getClickInfo() const {
        return _clickInfo;
    }

    const MapPos& VectorTileClickInfo::getClickPos() const {
        return _clickPos;
    }
        
    MapPos VectorTileClickInfo::getFeaturePos() const {
        std::shared_ptr<Geometry> geometry = _feature ? _feature->getGeometry() : std::shared_ptr<Geometry>();
        if (!geometry) {
            return MapPos();
        }
        if (_featurePosIndex >= 0 && geometry->getType() == GeometryType::GEOMETRY_TYPE_MULTIPOINT) {
            auto multiGeometry = std::static_pointer_cast<MultiGeometry>(geometry);
            if (_featurePosIndex < multiGeometry->getGeometryCount()) {
                return multiGeometry->getGeometry(_featurePosIndex)->getCenterPos();
            }
        }
        return geometry->getCenterPos();
    }

    const MapPos& VectorTileClickInfo::getFeatureClickPos() const {
        return _featureClickPos;
    }
    
    const MapTile& VectorTileClickInfo::getMapTile() const {
        return _feature->getMapTile();
    }

    long long VectorTileClickInfo::getFeatureId() const {
        return _feature->getId();
    }

    int VectorTileClickInfo::getFeaturePosIndex() const {
        return _featurePosIndex;
    }

    std::shared_ptr<VectorTileFeature> VectorTileClickInfo::getFeature() const {
        return _feature;
    }
    
    const std::string& VectorTileClickInfo::getFeatureLayerName() const {
        return _feature->getLayerName();
    }
    
    std::shared_ptr<Layer> VectorTileClickInfo::getLayer() const {
        return _layer;
    }

}

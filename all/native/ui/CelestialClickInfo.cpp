#include "CelestialClickInfo.h"
#include "celestial/CelestialObject.h"

namespace massif {

    CelestialClickInfo::CelestialClickInfo(const ClickInfo& clickInfo, const std::shared_ptr<CelestialObject>& celestialObject) :
        _clickInfo(clickInfo),
        _celestialObject(celestialObject),
        _azimuth(celestialObject ? celestialObject->getAzimuth() : 0.0f),
        _altitude(celestialObject ? celestialObject->getAltitude() : 0.0f)
    {
    }

    CelestialClickInfo::~CelestialClickInfo() {
    }

    ClickType::ClickType CelestialClickInfo::getClickType() const {
        return _clickInfo.getClickType();
    }

    const ClickInfo& CelestialClickInfo::getClickInfo() const {
        return _clickInfo;
    }

    std::shared_ptr<CelestialObject> CelestialClickInfo::getCelestialObject() const {
        return _celestialObject;
    }

    float CelestialClickInfo::getAzimuth() const {
        return _azimuth;
    }

    float CelestialClickInfo::getAltitude() const {
        return _altitude;
    }

}

#include "CelestialObject.h"
#include "layers/CelestialLayer.h"
#include "utils/Const.h"

#include <cmath>

namespace massif {

    CelestialObject::CelestialObject() :
        _mutex(),
        _directionAnchored(true),
        _azimuth(0.0f),
        _altitude(45.0f),
        _distance(0.0),
        _position(0, 0, 0),
        _positionAltitude(0.0),
        _color(0xFFFFFFFF),
        _visible(true),
        _occludedByMap(true),
        _metaData(),
        _layer()
    {
    }

    CelestialObject::~CelestialObject() {
    }

    bool CelestialObject::isDirectionAnchored() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _directionAnchored;
    }

    float CelestialObject::getAzimuth() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _azimuth;
    }

    float CelestialObject::getAltitude() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _altitude;
    }

    double CelestialObject::getDistance() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _distance;
    }

    void CelestialObject::setDirection(float azimuth, float altitude, double distance) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _directionAnchored = true;
            _azimuth = azimuth;
            _altitude = std::max(-90.0f, std::min(90.0f, altitude));
            _distance = std::max(0.0, distance);
        }
        notifyChanged();
    }

    MapPos CelestialObject::getPosition() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _position;
    }

    double CelestialObject::getPositionAltitude() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _positionAltitude;
    }

    void CelestialObject::setPosition(const MapPos& pos, double altitude) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _directionAnchored = false;
            _position = pos;
            _positionAltitude = altitude;
        }
        notifyChanged();
    }

    Color CelestialObject::getColor() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _color;
    }

    void CelestialObject::setColor(const Color& color) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _color = color;
        }
        notifyChanged();
    }

    bool CelestialObject::isVisible() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _visible;
    }

    void CelestialObject::setVisible(bool visible) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _visible = visible;
        }
        notifyChanged();
    }

    bool CelestialObject::isOccludedByMap() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _occludedByMap;
    }

    void CelestialObject::setOccludedByMap(bool occluded) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _occludedByMap = occluded;
        }
        notifyChanged();
    }

    Variant CelestialObject::getMetaDataElement(const std::string& key) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _metaData.find(key);
        if (it == _metaData.end()) {
            return Variant();
        }
        return it->second;
    }

    void CelestialObject::setMetaDataElement(const std::string& key, const Variant& element) {
        std::lock_guard<std::mutex> lock(_mutex);
        _metaData[key] = element;
    }

    cglib::vec3<double> CelestialObject::calculateDirectionVector() const {
        std::lock_guard<std::mutex> lock(_mutex);
        // Same convention as LightOptions::getSunDirection - x east, y north, z up, azimuth
        // clockwise from north - so an application can hand a direction straight over.
        double az = _azimuth * Const::DEG_TO_RAD;
        double alt = _altitude * Const::DEG_TO_RAD;
        double cosAlt = std::cos(alt);
        return cglib::vec3<double>(cosAlt * std::sin(az), cosAlt * std::cos(az), std::sin(alt));
    }

    void CelestialObject::setComponents(const std::shared_ptr<CelestialLayer>& layer) {
        std::lock_guard<std::mutex> lock(_mutex);
        _layer = layer;
    }

    std::shared_ptr<CelestialLayer> CelestialObject::getLayer() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _layer.lock();
    }

    void CelestialObject::notifyChanged() {
        if (std::shared_ptr<CelestialLayer> layer = getLayer()) {
            layer->refresh();
        }
    }

}

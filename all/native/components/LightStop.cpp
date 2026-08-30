#include "LightStop.h"

#include <algorithm>
#include <functional>
#include <sstream>

namespace massif {

    LightStop::LightStop() :
        _sunAltitude(0.0f),
        _ambientColor(255, 255, 255, 255),
        _ambientIntensity(1.0f),
        _sunColor(255, 255, 255, 255),
        _sunIntensity(0.0f)
    {
    }

    LightStop::LightStop(float sunAltitude, const Color& ambientColor, float ambientIntensity, const Color& sunColor, float sunIntensity) :
        _sunAltitude(std::max(-90.0f, std::min(90.0f, sunAltitude))),
        _ambientColor(ambientColor),
        _ambientIntensity(std::max(0.0f, std::min(1.0f, ambientIntensity))),
        _sunColor(sunColor),
        _sunIntensity(std::max(0.0f, std::min(1.0f, sunIntensity)))
    {
    }

    float LightStop::getSunAltitude() const {
        return _sunAltitude;
    }

    const Color& LightStop::getAmbientColor() const {
        return _ambientColor;
    }

    float LightStop::getAmbientIntensity() const {
        return _ambientIntensity;
    }

    const Color& LightStop::getSunColor() const {
        return _sunColor;
    }

    float LightStop::getSunIntensity() const {
        return _sunIntensity;
    }

    bool LightStop::operator ==(const LightStop& other) const {
        return _sunAltitude == other._sunAltitude && _ambientColor == other._ambientColor &&
               _ambientIntensity == other._ambientIntensity && _sunColor == other._sunColor &&
               _sunIntensity == other._sunIntensity;
    }

    bool LightStop::operator !=(const LightStop& other) const {
        return !(*this == other);
    }

    int LightStop::hash() const {
        std::hash<float> hasher;
        return static_cast<int>((hasher(_sunAltitude) << 16) ^ hasher(_ambientIntensity) ^
                                (static_cast<std::size_t>(_ambientColor.getARGB()) << 8) ^
                                static_cast<std::size_t>(_sunColor.getARGB()) ^ hasher(_sunIntensity));
    }

    std::string LightStop::toString() const {
        std::stringstream ss;
        ss << "LightStop [sunAltitude=" << _sunAltitude
           << ", ambientColor=" << _ambientColor.toString() << ", ambientIntensity=" << _ambientIntensity
           << ", sunColor=" << _sunColor.toString() << ", sunIntensity=" << _sunIntensity << "]";
        return ss.str();
    }

}

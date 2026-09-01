#include "LightOptions.h"
#include "utils/Const.h"

#include <algorithm>
#include <cmath>

namespace massif {

    LightOptions::LightOptions() :
        _sunAzimuth(315.0f),
        _sunAltitude(45.0f),
        _sunColorARGB(Color(255, 255, 255, 255).getARGB()),
        _sunIntensity(1.0f),
        // Full ambient and a real shadow strength: the values every terrain bench and every example
        // screenshot was made with. Both only take effect once an app turns terrain lighting on, so
        // nothing changes for a map that does not ask for it.
        _ambientIntensity(1.0f),
        _ambientColorARGB(Color(255, 255, 255, 255).getARGB()),
        _sunOverridesStyle(false),
        _dayCycleLights(false),
        _dayCycleLightStops(),
        _dayCycleRisingLightStops(),
        _dayCycleLightStopsMutex(),
        _terrainLightingEnabled(false),
        // mapbox's `shadow-intensity` default. With the direct-share scaling in resolveLighting
        // this is their shadow exactly, so 1 is the physical depth rather than the maximum.
        _shadowStrength(1.0f),
        // mapbox's 2048 px map (shadow_renderer.ts _shadowParameters), over THREE cascades rather
        // than their two: measured side by side, the extra page is worth its cost here. It only
        // became so once the cutout went back to 4.5 - at 2.5 the ladder divided down to 0.28x the
        // camera-to-focus distance, well ABOVE the nearest ground on screen (0.84x at tilt 55), so
        // two of the three pages held nothing. Costs a 6144 x 2048 depth24 atlas, 50 MB.
        _shadowMapSize(2048),
        _shadowCascades(3),
        // 1.0 shadow-map texels. 0.25 leaves acne on a lit slope at this cascade count.
        _shadowBias(1.0f),
        _shadowNormalOffset(3.0f),
        _shadowSoftness(1.0f),
        _shadowDistance(0.0f), // 0 = the built-in 4.5, which is mapbox's
        _shadowCasterMargin(3),
        _onChangeListeners(),
        _onChangeListenersMutex()
    {
    }

    LightOptions::~LightOptions() {
    }

    float LightOptions::getSunAzimuth() const {
        return _sunAzimuth.load();
    }

    void LightOptions::setSunAzimuth(float azimuth) {
        float wrapped = std::fmod(azimuth, 360.0f);
        if (wrapped < 0) {
            wrapped += 360.0f;
        }
        if (_sunAzimuth.exchange(wrapped) != wrapped) {
            notifyOptionChanged("SunAzimuth");
        }
    }

    float LightOptions::getSunAltitude() const {
        return _sunAltitude.load();
    }

    void LightOptions::setSunAltitude(float altitude) {
        float clamped = std::max(-90.0f, std::min(90.0f, altitude));
        if (_sunAltitude.exchange(clamped) != clamped) {
            notifyOptionChanged("SunAltitude");
        }
    }

    void LightOptions::setSunPositionFromTime(int year, int month, int day, int hour, int minute, double latitude, double longitude) {
        // NOAA solar position algorithm, in the low-accuracy form that is good to ~0.1 degree.
        // Julian day for the given UTC instant.
        int a = (14 - month) / 12;
        int y = year + 4800 - a;
        int m = month + 12 * a - 3;
        double jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
        double jd = jdn + (hour - 12) / 24.0 + minute / 1440.0;
        double n = jd - 2451545.0;

        double meanLong = std::fmod(280.460 + 0.9856474 * n, 360.0);
        double meanAnom = std::fmod(357.528 + 0.9856003 * n, 360.0) * Const::DEG_TO_RAD;
        double eclipticLong = (meanLong + 1.915 * std::sin(meanAnom) + 0.020 * std::sin(2 * meanAnom)) * Const::DEG_TO_RAD;
        double obliquity = (23.439 - 0.0000004 * n) * Const::DEG_TO_RAD;

        double rightAsc = std::atan2(std::cos(obliquity) * std::sin(eclipticLong), std::cos(eclipticLong));
        double decl = std::asin(std::sin(obliquity) * std::sin(eclipticLong));

        // Greenwich mean sidereal time, then the local hour angle.
        double gmst = std::fmod(18.697374558 + 24.06570982441908 * n, 24.0);
        if (gmst < 0) {
            gmst += 24.0;
        }
        double lmst = gmst * 15.0 * Const::DEG_TO_RAD + longitude * Const::DEG_TO_RAD;
        double hourAngle = lmst - rightAsc;

        double lat = latitude * Const::DEG_TO_RAD;
        double altitude = std::asin(std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(hourAngle));
        double azimuth = std::atan2(std::sin(hourAngle), std::cos(hourAngle) * std::sin(lat) - std::tan(decl) * std::cos(lat));
        // atan2 above is measured from south; convert to the clockwise-from-north convention.
        azimuth = azimuth * Const::RAD_TO_DEG + 180.0;

        setSunAzimuth(static_cast<float>(azimuth));
        setSunAltitude(static_cast<float>(altitude * Const::RAD_TO_DEG));
    }

    Color LightOptions::getSunColor() const {
        return Color(_sunColorARGB.load());
    }

    void LightOptions::setSunColor(const Color& color) {
        if (_sunColorARGB.exchange(color.getARGB()) != color.getARGB()) {
            notifyOptionChanged("SunColor");
        }
    }

    float LightOptions::getSunIntensity() const {
        return _sunIntensity.load();
    }

    void LightOptions::setSunIntensity(float intensity) {
        float clamped = std::max(0.0f, std::min(8.0f, intensity));
        if (_sunIntensity.exchange(clamped) != clamped) {
            notifyOptionChanged("SunIntensity");
        }
    }

    float LightOptions::getAmbientIntensity() const {
        return _ambientIntensity.load();
    }

    void LightOptions::setAmbientIntensity(float intensity) {
        float clamped = std::max(0.0f, std::min(1.0f, intensity));
        if (_ambientIntensity.exchange(clamped) != clamped) {
            notifyOptionChanged("AmbientIntensity");
        }
    }

    Color LightOptions::getAmbientColor() const {
        return Color(_ambientColorARGB.load());
    }

    void LightOptions::setAmbientColor(const Color& color) {
        if (_ambientColorARGB.exchange(color.getARGB()) != color.getARGB()) {
            notifyOptionChanged("AmbientColor");
        }
    }

    bool LightOptions::isSunOverridingStyle() const {
        return _sunOverridesStyle.load();
    }

    void LightOptions::setSunOverridingStyle(bool overriding) {
        if (_sunOverridesStyle.exchange(overriding) != overriding) {
            notifyOptionChanged("SunOverridingStyle");
        }
    }

    bool LightOptions::isDayCycleLightsEnabled() const {
        return _dayCycleLights.load();
    }

    void LightOptions::setDayCycleLightsEnabled(bool enabled) {
        if (_dayCycleLights.exchange(enabled) != enabled) {
            notifyOptionChanged("DayCycleLightsEnabled");
        }
    }

    std::vector<LightStop> LightOptions::getDayCycleLightStops() const {
        std::lock_guard<std::mutex> lock(_dayCycleLightStopsMutex);
        return _dayCycleLightStops;
    }

    void LightOptions::setDayCycleLightStops(const std::vector<LightStop>& stops) {
        {
            std::lock_guard<std::mutex> lock(_dayCycleLightStopsMutex);
            if (_dayCycleLightStops == stops) {
                return;
            }
            _dayCycleLightStops = stops;
        }
        notifyOptionChanged("DayCycleLightStops");
    }

    std::vector<LightStop> LightOptions::getDayCycleRisingLightStops() const {
        std::lock_guard<std::mutex> lock(_dayCycleLightStopsMutex);
        return _dayCycleRisingLightStops;
    }

    void LightOptions::setDayCycleRisingLightStops(const std::vector<LightStop>& stops) {
        {
            std::lock_guard<std::mutex> lock(_dayCycleLightStopsMutex);
            if (_dayCycleRisingLightStops == stops) {
                return;
            }
            _dayCycleRisingLightStops = stops;
        }
        notifyOptionChanged("DayCycleRisingLightStops");
    }

    bool LightOptions::isTerrainLightingEnabled() const {
        return _terrainLightingEnabled.load();
    }

    void LightOptions::setTerrainLightingEnabled(bool enabled) {
        if (_terrainLightingEnabled.exchange(enabled) != enabled) {
            notifyOptionChanged("TerrainLightingEnabled");
        }
    }

    float LightOptions::getShadowStrength() const {
        return _shadowStrength.load();
    }

    void LightOptions::setShadowStrength(float strength) {
        // No upper bound: 1 is the physically correct depth, so exaggerating past it is a look
        // choice a style or an app is allowed to make. resolveLighting clamps what it resolves to.
        float value = std::max(0.0f, strength);
        if (_shadowStrength.exchange(value) != value) {
            notifyOptionChanged("ShadowStrength");
        }
    }

    int LightOptions::getShadowMapSize() const {
        return _shadowMapSize.load();
    }

    void LightOptions::setShadowMapSize(int size) {
        int value = std::min(4096, std::max(256, size));
        if (_shadowMapSize.exchange(value) != value) {
            notifyOptionChanged("ShadowMapSize");
        }
    }

    float LightOptions::getShadowSoftness() const {
        return _shadowSoftness.load();
    }

    void LightOptions::setShadowSoftness(float softness) {
        float value = std::min(8.0f, std::max(0.0f, softness));
        if (_shadowSoftness.exchange(value) != value) {
            notifyOptionChanged("ShadowSoftness");
        }
    }

    int LightOptions::getShadowCascades() const {
        return _shadowCascades.load();
    }

    void LightOptions::setShadowCascades(int cascades) {
        int clamped = std::max(1, std::min(4, cascades));
        if (_shadowCascades.exchange(clamped) != clamped) {
            notifyOptionChanged("ShadowCascades");
        }
    }

    float LightOptions::getShadowDistance() const {
        return _shadowDistance.load();
    }

    void LightOptions::setShadowDistance(float distance) {
        float value = std::max(0.0f, distance);
        if (_shadowDistance.exchange(value) != value) {
            notifyOptionChanged("ShadowDistance");
        }
    }

    int LightOptions::getShadowCasterMargin() const {
        return _shadowCasterMargin.load();
    }

    void LightOptions::setShadowCasterMargin(int margin) {
        int value = std::min(8, std::max(0, margin));
        if (_shadowCasterMargin.exchange(value) != value) {
            notifyOptionChanged("ShadowCasterMargin");
        }
    }

    float LightOptions::getShadowBias() const {
        return _shadowBias.load();
    }

    void LightOptions::setShadowBias(float bias) {
        bias = std::min(50.0f, std::max(0.0f, bias));
        if (_shadowBias.exchange(bias) != bias) {
            notifyOptionChanged("ShadowBias");
        }
    }

    float LightOptions::getShadowNormalOffset() const {
        return _shadowNormalOffset.load();
    }

    void LightOptions::setShadowNormalOffset(float offset) {
        offset = std::min(16.0f, std::max(0.0f, offset));
        if (_shadowNormalOffset.exchange(offset) != offset) {
            notifyOptionChanged("ShadowNormalOffset");
        }
    }

    cglib::vec3<float> LightOptions::getSunDirection() const {
        // Internal map coordinates: x east, y north, z up. Azimuth is clockwise from north.
        double az = _sunAzimuth.load() * Const::DEG_TO_RAD;
        double alt = _sunAltitude.load() * Const::DEG_TO_RAD;
        double cosAlt = std::cos(alt);
        return cglib::vec3<float>(static_cast<float>(cosAlt * std::sin(az)),
                                  static_cast<float>(cosAlt * std::cos(az)),
                                  static_cast<float>(std::sin(alt)));
    }

    void LightOptions::registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.push_back(listener);
    }

    void LightOptions::unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.erase(std::remove(_onChangeListeners.begin(), _onChangeListeners.end(), listener), _onChangeListeners.end());
    }

    void LightOptions::notifyOptionChanged(const std::string& optionName) {
        std::vector<std::shared_ptr<OnChangeListener> > onChangeListeners;
        {
            std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
            onChangeListeners = _onChangeListeners;
        }
        for (const std::shared_ptr<OnChangeListener>& listener : onChangeListeners) {
            listener->onLightOptionChanged(optionName);
        }
    }
}

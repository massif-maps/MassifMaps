#include "SunExposureOptions.h"

#include <algorithm>

namespace massif {

    SunExposureOptions::SunExposureOptions() :
        _eyeHeight(1.5),
        _maxDistance(30000),
        _discRadius(0.2666),
        _darkBelow(-0.2666),
        _lowSunBelow(2),
        _firstStep(10),
        _stepGrowth(0.003),
        _lodRatio(1.0 / 300.0),
        _minTexel(5),
        _refraction(0.13),
        _loadMissingTiles(true),
        _loadTimeoutMs(20000),
        _cacheMB(96),
        _releaseCache(false)
    {
    }

    double SunExposureOptions::getEyeHeight() const { return _eyeHeight; }
    void SunExposureOptions::setEyeHeight(double eyeHeight) { _eyeHeight = std::max(0.0, eyeHeight); }

    double SunExposureOptions::getMaxDistance() const { return _maxDistance; }
    void SunExposureOptions::setMaxDistance(double maxDistance) { _maxDistance = std::max(100.0, std::min(200000.0, maxDistance)); }

    double SunExposureOptions::getDiscRadius() const { return _discRadius; }
    void SunExposureOptions::setDiscRadius(double discRadius) { _discRadius = std::max(0.0, discRadius); }

    double SunExposureOptions::getDarkBelow() const { return _darkBelow; }
    void SunExposureOptions::setDarkBelow(double darkBelow) { _darkBelow = darkBelow; }

    double SunExposureOptions::getLowSunBelow() const { return _lowSunBelow; }
    void SunExposureOptions::setLowSunBelow(double lowSunBelow) { _lowSunBelow = lowSunBelow; }

    double SunExposureOptions::getFirstStep() const { return _firstStep; }
    void SunExposureOptions::setFirstStep(double firstStep) { _firstStep = std::max(0.1, firstStep); }

    double SunExposureOptions::getStepGrowth() const { return _stepGrowth; }
    void SunExposureOptions::setStepGrowth(double stepGrowth) { _stepGrowth = std::max(1e-5, stepGrowth); }

    double SunExposureOptions::getLodRatio() const { return _lodRatio; }
    void SunExposureOptions::setLodRatio(double lodRatio) { _lodRatio = std::max(1e-5, lodRatio); }

    double SunExposureOptions::getMinTexel() const { return _minTexel; }
    void SunExposureOptions::setMinTexel(double minTexel) { _minTexel = std::max(0.1, minTexel); }

    double SunExposureOptions::getRefraction() const { return _refraction; }
    void SunExposureOptions::setRefraction(double refraction) { _refraction = std::max(0.0, std::min(0.5, refraction)); }

    bool SunExposureOptions::isLoadMissingTiles() const { return _loadMissingTiles; }
    void SunExposureOptions::setLoadMissingTiles(bool loadMissingTiles) { _loadMissingTiles = loadMissingTiles; }

    int SunExposureOptions::getLoadTimeoutMs() const { return _loadTimeoutMs; }
    void SunExposureOptions::setLoadTimeoutMs(int loadTimeoutMs) { _loadTimeoutMs = std::max(0, loadTimeoutMs); }

    int SunExposureOptions::getCacheMB() const { return _cacheMB; }
    void SunExposureOptions::setCacheMB(int cacheMB) { _cacheMB = std::max(1, cacheMB); }

    bool SunExposureOptions::isReleaseCache() const { return _releaseCache; }
    void SunExposureOptions::setReleaseCache(bool releaseCache) { _releaseCache = releaseCache; }

}

#include "FogOptions.h"

#include <algorithm>

namespace massif {

    FogOptions::FogOptions() :
        _enabled(true),
        _colorARGB(Color(0, 0, 0, 0).getARGB()),
        _rangeStart(0.8f),
        _rangeEnd(8.0f),
        _highColorARGB(Color(0, 0, 0, 0).getARGB()),
        _spaceColorARGB(Color(0, 0, 0, 0).getARGB()),
        _horizonBlend(12.0f / 90.0f),
        _verticalRangeStart(0.0f),
        _verticalRangeEnd(0.0f),
        _starIntensity(0.0f),
        _shaderSource(),
        _shaderSourceMutex(),
        _onChangeListeners(),
        _onChangeListenersMutex()
    {
    }

    FogOptions::~FogOptions() {
    }

    bool FogOptions::isEnabled() const {
        return _enabled.load();
    }

    void FogOptions::setEnabled(bool enabled) {
        if (_enabled.exchange(enabled) != enabled) {
            notifyOptionChanged("Enabled");
        }
    }

    Color FogOptions::getColor() const {
        return Color(_colorARGB.load());
    }

    void FogOptions::setColor(const Color& color) {
        if (_colorARGB.exchange(color.getARGB()) != color.getARGB()) {
            notifyOptionChanged("Color");
        }
    }

    float FogOptions::getRangeStart() const {
        return _rangeStart.load();
    }

    void FogOptions::setRangeStart(float rangeStart) {
        float clamped = std::max(0.0f, rangeStart);
        if (_rangeStart.exchange(clamped) != clamped) {
            notifyOptionChanged("RangeStart");
        }
    }

    float FogOptions::getRangeEnd() const {
        return _rangeEnd.load();
    }

    void FogOptions::setRangeEnd(float rangeEnd) {
        float clamped = std::max(0.0f, rangeEnd);
        if (_rangeEnd.exchange(clamped) != clamped) {
            notifyOptionChanged("RangeEnd");
        }
    }

    Color FogOptions::getHighColor() const {
        return Color(_highColorARGB.load());
    }

    void FogOptions::setHighColor(const Color& color) {
        if (_highColorARGB.exchange(color.getARGB()) != color.getARGB()) {
            notifyOptionChanged("HighColor");
        }
    }

    Color FogOptions::getSpaceColor() const {
        return Color(_spaceColorARGB.load());
    }

    void FogOptions::setSpaceColor(const Color& color) {
        if (_spaceColorARGB.exchange(color.getARGB()) != color.getARGB()) {
            notifyOptionChanged("SpaceColor");
        }
    }

    float FogOptions::getHorizonBlend() const {
        return _horizonBlend.load();
    }

    void FogOptions::setHorizonBlend(float horizonBlend) {
        float clamped = std::max(0.0f, std::min(1.0f, horizonBlend));
        if (_horizonBlend.exchange(clamped) != clamped) {
            notifyOptionChanged("HorizonBlend");
        }
    }

    float FogOptions::getVerticalRangeStart() const {
        return _verticalRangeStart.load();
    }

    void FogOptions::setVerticalRangeStart(float startMeters) {
        float clamped = std::max(0.0f, startMeters);
        if (_verticalRangeStart.exchange(clamped) != clamped) {
            notifyOptionChanged("VerticalRangeStart");
        }
    }

    float FogOptions::getVerticalRangeEnd() const {
        return _verticalRangeEnd.load();
    }

    void FogOptions::setVerticalRangeEnd(float endMeters) {
        float clamped = std::max(0.0f, endMeters);
        if (_verticalRangeEnd.exchange(clamped) != clamped) {
            notifyOptionChanged("VerticalRangeEnd");
        }
    }

    float FogOptions::getStarIntensity() const {
        return _starIntensity.load();
    }

    void FogOptions::setStarIntensity(float starIntensity) {
        float clamped = std::max(0.0f, std::min(1.0f, starIntensity));
        if (_starIntensity.exchange(clamped) != clamped) {
            notifyOptionChanged("StarIntensity");
        }
    }

    std::string FogOptions::getShaderSource() const {
        std::lock_guard<std::mutex> lock(_shaderSourceMutex);
        return _shaderSource;
    }

    void FogOptions::setShaderSource(const std::string& shaderSource) {
        {
            std::lock_guard<std::mutex> lock(_shaderSourceMutex);
            if (_shaderSource == shaderSource) {
                return;
            }
            _shaderSource = shaderSource;
        }
        notifyOptionChanged("ShaderSource");
    }

    void FogOptions::registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.push_back(listener);
    }

    void FogOptions::unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.erase(std::remove(_onChangeListeners.begin(), _onChangeListeners.end(), listener), _onChangeListeners.end());
    }

    void FogOptions::notifyOptionChanged(const std::string& optionName) {
        std::vector<std::shared_ptr<OnChangeListener> > onChangeListeners;
        {
            std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
            onChangeListeners = _onChangeListeners;
        }
        for (const std::shared_ptr<OnChangeListener>& listener : onChangeListeners) {
            listener->onFogOptionChanged(optionName);
        }
    }
}

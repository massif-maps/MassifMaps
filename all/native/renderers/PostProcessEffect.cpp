#include "PostProcessEffect.h"

namespace massif {

    PostProcessEffect::PostProcessEffect(const std::string& name, const std::string& fragmentShader) :
        _name(name),
        _fragmentShader(fragmentShader),
        _terrainDepthRequired(false),
        _terrainNormalsRequired(false),
        _floatParameters(),
        _colorParameters(),
        _mutex()
    {
    }

    PostProcessEffect::~PostProcessEffect() {
    }

    const std::string& PostProcessEffect::getName() const {
        return _name;
    }

    const std::string& PostProcessEffect::getFragmentShader() const {
        return _fragmentShader;
    }

    bool PostProcessEffect::isTerrainDepthRequired() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _terrainDepthRequired;
    }

    void PostProcessEffect::setTerrainDepthRequired(bool required) {
        std::lock_guard<std::mutex> lock(_mutex);
        _terrainDepthRequired = required;
    }

    bool PostProcessEffect::isTerrainNormalsRequired() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _terrainNormalsRequired;
    }

    void PostProcessEffect::setTerrainNormalsRequired(bool required) {
        std::lock_guard<std::mutex> lock(_mutex);
        _terrainNormalsRequired = required;
        if (required) {
            // One texture: the normals are packed by the depth pre-pass.
            _terrainDepthRequired = true;
        }
    }

    float PostProcessEffect::getFloatParameter(const std::string& name) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _floatParameters.find(name);
        return it != _floatParameters.end() ? it->second : 0.0f;
    }

    void PostProcessEffect::setFloatParameter(const std::string& name, float value) {
        std::lock_guard<std::mutex> lock(_mutex);
        _floatParameters[name] = value;
    }

    Color PostProcessEffect::getColorParameter(const std::string& name) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _colorParameters.find(name);
        return it != _colorParameters.end() ? it->second : Color(0, 0, 0, 0);
    }

    void PostProcessEffect::setColorParameter(const std::string& name, const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);
        _colorParameters[name] = color;
    }

    std::map<std::string, float> PostProcessEffect::getFloatParameters() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _floatParameters;
    }

    std::map<std::string, Color> PostProcessEffect::getColorParameters() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _colorParameters;
    }

}

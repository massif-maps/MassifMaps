#include "TerrainOptions.h"
#include "components/Exceptions.h"
#include "datasources/TileDataSource.h"
#include "terrain/ElevationManager.h"

#include <algorithm>

namespace massif {

    const std::string TerrainOptions::DEFAULT_NO_DRAPE_LAYER_FILTER = "^contour|maneuver.*";

    TerrainOptions::TerrainOptions(const std::shared_ptr<TileDataSource>& dataSource) :
        TerrainOptions(dataSource, std::shared_ptr<ElevationDecoder>())
    {
    }

    TerrainOptions::TerrainOptions(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& elevationDecoder) :
        _dataSource(dataSource),
        _elevationManager(dataSource ? std::make_shared<ElevationManager>(dataSource, elevationDecoder) : std::shared_ptr<ElevationManager>()),
        _enabled(true),
        // 64 triangles per tile side, which is what tangram uses (RasterStyle::build) and what every
        // bench here was run at. 32 leaves draped content visibly floating over the ground; 128
        // measured 8.5 fps against 15.2 at 64 on the Crosscall.
        _meshResolution(64),
        _tileEdgeStitchingEnabled(true),
        _drapeFillsEnabled(true),
        _drapeLinesEnabled(true),
        _drapeResolution(1024),
        _minZoom(5),
        _maxTileZoomOffset(100),
        _backgroundColorARGB(0),
        _backgroundBitmapEnabled(false),
        _depthBias(0.0002f),
        // 60 m, not 200: 200 stops the camera well short of the surface, so a close approach swings
        // the view into the nearest hillside instead of flying between the peaks.
        _cameraClearance(60.0f),
        _cameraClampDuration(0.0f),
        _billboardOcclusionEnabled(true),
        _billboardOcclusionTolerance(0.0f),
        _textOcclusionOpacity(1.0f),
        _viewDistanceFactor(1.0f),
        _viewDistance(0.0f),
        // 3, not the demo's 8: 8 only pays for itself next to the demo's fixed 170 km view, where
        // what it coarsens is the far horizon. On the default view distance it coarsens tiles that
        // are still large on screen, and the result is a blurred band with a hard tile edge down
        // the middle of the view.
        _maxTileZoomCoarsening(3),
        _noDrapeLayerFilter(DEFAULT_NO_DRAPE_LAYER_FILTER),
        _surfaceShaderSource(),
        _surfaceParameters(),
        _surfaceColorParameters(),
        _surfaceMutex(),
        _onChangeListeners(),
        _onChangeListenersMutex()
    {
        if (!dataSource) {
            throw NullArgumentException("Null dataSource");
        }
    }

    TerrainOptions::~TerrainOptions() {
    }

    std::shared_ptr<TileDataSource> TerrainOptions::getDataSource() const {
        return _dataSource;
    }

    std::shared_ptr<ElevationDecoder> TerrainOptions::getElevationDecoder() const {
        return _elevationManager->getElevationDecoder();
    }

    bool TerrainOptions::isEnabled() const {
        return _enabled.load();
    }

    void TerrainOptions::setEnabled(bool enabled) {
        if (_enabled.exchange(enabled) != enabled) {
            notifyOptionChanged("Enabled");
        }
    }

    float TerrainOptions::getExaggeration() const {
        return _elevationManager->getExaggeration();
    }

    void TerrainOptions::setExaggeration(float exaggeration) {
        if (_elevationManager->getExaggeration() != exaggeration) {
            _elevationManager->setExaggeration(exaggeration);
            notifyOptionChanged("Exaggeration");
        }
    }

    bool TerrainOptions::isSeamlessTileEdgesEnabled() const {
        return _elevationManager->isSeamlessTileEdgesEnabled();
    }

    void TerrainOptions::setSeamlessTileEdgesEnabled(bool enabled) {
        if (_elevationManager->isSeamlessTileEdgesEnabled() != enabled) {
            _elevationManager->setSeamlessTileEdgesEnabled(enabled);
            notifyOptionChanged("SeamlessTileEdgesEnabled");
        }
    }

    bool TerrainOptions::isElevationPrefetchEnabled() const {
        return _elevationManager->isNeighbourPrefetchEnabled();
    }

    void TerrainOptions::setElevationPrefetchEnabled(bool enabled) {
        if (_elevationManager->isNeighbourPrefetchEnabled() != enabled) {
            _elevationManager->setNeighbourPrefetchEnabled(enabled);
            notifyOptionChanged("ElevationPrefetchEnabled");
        }
    }

    int TerrainOptions::getMeshResolution() const {
        return _meshResolution.load();
    }

    void TerrainOptions::setMeshResolution(int meshResolution) {
        int resolution = std::min(256, std::max(2, meshResolution));
        if (_meshResolution.exchange(resolution) != resolution) {
            notifyOptionChanged("MeshResolution");
        }
    }

    bool TerrainOptions::isTileEdgeStitchingEnabled() const {
        return _tileEdgeStitchingEnabled.load();
    }

    void TerrainOptions::setTileEdgeStitchingEnabled(bool enabled) {
        if (_tileEdgeStitchingEnabled.exchange(enabled) != enabled) {
            notifyOptionChanged("TileEdgeStitchingEnabled");
        }
    }

    bool TerrainOptions::isDrapeFillsEnabled() const {
        return _drapeFillsEnabled.load();
    }

    void TerrainOptions::setDrapeFillsEnabled(bool enabled) {
        if (_drapeFillsEnabled.exchange(enabled) != enabled) {
            notifyOptionChanged("DrapeFillsEnabled");
        }
    }

    bool TerrainOptions::isDrapeLinesEnabled() const {
        return _drapeLinesEnabled.load();
    }

    std::string TerrainOptions::getNoDrapeLayerFilter() const {
        std::lock_guard<std::mutex> lock(_noDrapeMutex);
        return _noDrapeLayerFilter;
    }

    void TerrainOptions::setNoDrapeLayerFilter(const std::string& filter) {
        {
            std::lock_guard<std::mutex> lock(_noDrapeMutex);
            if (_noDrapeLayerFilter == filter) {
                return;
            }
            _noDrapeLayerFilter = filter;
        }
        notifyOptionChanged("NoDrapeLayerFilter");
    }

    void TerrainOptions::setDrapeLinesEnabled(bool enabled) {
        if (_drapeLinesEnabled.exchange(enabled) != enabled) {
            notifyOptionChanged("DrapeLinesEnabled");
        }
    }

    int TerrainOptions::getDrapeResolution() const {
        return _drapeResolution.load();
    }

    void TerrainOptions::setDrapeResolution(int resolution) {
        int value = (resolution > 0 ? std::min(2048, std::max(128, resolution)) : 0);
        if (_drapeResolution.exchange(value) != value) {
            notifyOptionChanged("DrapeResolution");
        }
    }

    int TerrainOptions::getMinZoom() const {
        return _minZoom.load();
    }

    void TerrainOptions::setMinZoom(int minZoom) {
        int zoom = std::min(24, std::max(0, minZoom));
        if (_minZoom.exchange(zoom) != zoom) {
            notifyOptionChanged("MinZoom");
        }
    }

    std::string TerrainOptions::getSurfaceShaderSource() const {
        std::lock_guard<std::mutex> lock(_surfaceMutex);
        return _surfaceShaderSource;
    }

    void TerrainOptions::setSurfaceShaderSource(const std::string& shaderSource) {
        {
            std::lock_guard<std::mutex> lock(_surfaceMutex);
            if (_surfaceShaderSource == shaderSource) {
                return;
            }
            _surfaceShaderSource = shaderSource;
        }
        notifyOptionChanged("SurfaceShaderSource");
    }

    float TerrainOptions::getSurfaceParameter(const std::string& name) const {
        std::lock_guard<std::mutex> lock(_surfaceMutex);
        auto it = _surfaceParameters.find(name);
        return it != _surfaceParameters.end() ? it->second : 0.0f;
    }

    void TerrainOptions::setSurfaceParameter(const std::string& name, float value) {
        {
            std::lock_guard<std::mutex> lock(_surfaceMutex);
            auto it = _surfaceParameters.find(name);
            if (it != _surfaceParameters.end() && it->second == value) {
                return;
            }
            _surfaceParameters[name] = value;
        }
        notifyOptionChanged("SurfaceParameter");
    }

    Color TerrainOptions::getSurfaceColorParameter(const std::string& name) const {
        std::lock_guard<std::mutex> lock(_surfaceMutex);
        auto it = _surfaceColorParameters.find(name);
        return it != _surfaceColorParameters.end() ? it->second : Color(0, 0, 0, 0);
    }

    void TerrainOptions::setSurfaceColorParameter(const std::string& name, const Color& color) {
        {
            std::lock_guard<std::mutex> lock(_surfaceMutex);
            auto it = _surfaceColorParameters.find(name);
            if (it != _surfaceColorParameters.end() && it->second == color) {
                return;
            }
            _surfaceColorParameters[name] = color;
        }
        notifyOptionChanged("SurfaceColorParameter");
    }

    std::map<std::string, float> TerrainOptions::getSurfaceParameters() const {
        std::lock_guard<std::mutex> lock(_surfaceMutex);
        return _surfaceParameters;
    }

    std::map<std::string, Color> TerrainOptions::getSurfaceColorParameters() const {
        std::lock_guard<std::mutex> lock(_surfaceMutex);
        return _surfaceColorParameters;
    }

    int TerrainOptions::getMaxTileZoomCoarsening() const {
        return _maxTileZoomCoarsening.load();
    }

    void TerrainOptions::setMaxTileZoomCoarsening(int levels) {
        int clamped = std::max(0, levels);
        if (_maxTileZoomCoarsening.exchange(clamped) != clamped) {
            notifyOptionChanged("MaxTileZoomCoarsening");
        }
    }

    float TerrainOptions::getViewDistanceFactor() const {
        return _viewDistanceFactor.load();
    }

    void TerrainOptions::setViewDistanceFactor(float factor) {
        float clamped = std::max(0.0f, factor);
        if (_viewDistanceFactor.exchange(clamped) != clamped) {
            notifyOptionChanged("ViewDistanceFactor");
        }
    }

    float TerrainOptions::getViewDistance() const {
        return _viewDistance.load();
    }

    void TerrainOptions::setViewDistance(float distance) {
        float clamped = std::max(0.0f, distance);
        if (_viewDistance.exchange(clamped) != clamped) {
            notifyOptionChanged("ViewDistance");
        }
    }

    Color TerrainOptions::getBackgroundColor() const {
        return Color(_backgroundColorARGB.load());
    }

    void TerrainOptions::setBackgroundColor(const Color& color) {
        int value = color.getARGB();
        if (_backgroundColorARGB.exchange(value) != value) {
            notifyOptionChanged("BackgroundColor");
        }
    }

    bool TerrainOptions::isBackgroundBitmapEnabled() const {
        return _backgroundBitmapEnabled.load();
    }

    void TerrainOptions::setBackgroundBitmapEnabled(bool enabled) {
        if (_backgroundBitmapEnabled.exchange(enabled) != enabled) {
            notifyOptionChanged("BackgroundBitmapEnabled");
        }
    }

    int TerrainOptions::getMaxTileZoomOffset() const {
        return _maxTileZoomOffset.load();
    }

    void TerrainOptions::setMaxTileZoomOffset(int offset) {
        int value = std::min(100, std::max(-24, offset));
        if (_maxTileZoomOffset.exchange(value) != value) {
            notifyOptionChanged("MaxTileZoomOffset");
        }
    }

    float TerrainOptions::getCameraClearance() const {
        return _cameraClearance.load();
    }

    void TerrainOptions::setCameraClearance(float clearance) {
        float value = std::max(0.0f, clearance);
        if (_cameraClearance.exchange(value) != value) {
            notifyOptionChanged("CameraClearance");
        }
    }

    float TerrainOptions::getCameraClampDuration() const {
        return _cameraClampDuration.load();
    }

    void TerrainOptions::setCameraClampDuration(float duration) {
        float value = std::max(0.0f, duration);
        if (_cameraClampDuration.exchange(value) != value) {
            notifyOptionChanged("CameraClampDuration");
        }
    }

    float TerrainOptions::getDepthBias() const {
        return _depthBias.load();
    }

    void TerrainOptions::setDepthBias(float depthBias) {
        float bias = std::min(0.01f, std::max(0.0f, depthBias));
        if (_depthBias.exchange(bias) != bias) {
            notifyOptionChanged("DepthBias");
        }
    }

    float TerrainOptions::getBillboardOcclusionTolerance() const {
        return _billboardOcclusionTolerance.load();
    }

    void TerrainOptions::setBillboardOcclusionTolerance(float tolerance) {
        float value = std::min(1.0f, std::max(0.0f, tolerance));
        if (_billboardOcclusionTolerance.exchange(value) != value) {
            notifyOptionChanged("BillboardOcclusionTolerance");
        }
    }

    float TerrainOptions::getTextOcclusionOpacity() const {
        return _textOcclusionOpacity.load();
    }

    void TerrainOptions::setTextOcclusionOpacity(float opacity) {
        float value = std::min(1.0f, std::max(0.0f, opacity));
        if (_textOcclusionOpacity.exchange(value) != value) {
            notifyOptionChanged("TextOcclusionOpacity");
        }
    }

    bool TerrainOptions::isBillboardOcclusionEnabled() const {
        return _billboardOcclusionEnabled.load();
    }

    void TerrainOptions::setBillboardOcclusionEnabled(bool enabled) {
        if (_billboardOcclusionEnabled.exchange(enabled) != enabled) {
            notifyOptionChanged("BillboardOcclusionEnabled");
        }
    }

    std::size_t TerrainOptions::getElevationCacheCapacity() const {
        return _elevationManager->getCacheCapacity();
    }

    void TerrainOptions::setElevationCacheCapacity(std::size_t capacityInBytes) {
        _elevationManager->setCacheCapacity(capacityInBytes);
    }

    double TerrainOptions::getElevation(const MapPos& pos) const {
        return _elevationManager->getElevation(pos);
    }

    std::vector<double> TerrainOptions::getElevations(const std::vector<MapPos>& poses) const {
        return _elevationManager->getElevations(poses);
    }

    std::shared_ptr<ElevationManager> TerrainOptions::getElevationManager() const {
        return _elevationManager;
    }

    void TerrainOptions::registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.push_back(listener);
    }

    void TerrainOptions::unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.erase(std::remove(_onChangeListeners.begin(), _onChangeListeners.end(), listener), _onChangeListeners.end());
    }

    void TerrainOptions::notifyOptionChanged(const std::string& optionName) {
        std::vector<std::shared_ptr<OnChangeListener> > onChangeListeners;
        {
            std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
            onChangeListeners = _onChangeListeners;
        }
        for (const std::shared_ptr<OnChangeListener>& listener : onChangeListeners) {
            listener->onTerrainOptionChanged(optionName);
        }
    }
}

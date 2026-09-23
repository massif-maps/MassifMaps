#include "TerrainOptions.h"
#include "components/Exceptions.h"
#include "datasources/TileDataSource.h"
#include "terrain/CameraClearance.h"
#include "terrain/ElevationManager.h"
#include "utils/Log.h"

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
        _exaggeration(1.0f),
        _flattened(false),
        _flattenMode(TerrainFlattenMode::TERRAIN_FLATTEN_MODE_RENDER),
        _decodeActive(true),
        _flattenSwitchStarted(false),
        _flattenManual(false),
        _flattenManualRatio(0.0f),
        _switching(false),
        _flattenRatio(0.0f),
        // 2 px and 88 degrees, device-checked on the Crosscall: below 2 px the displacement is
        // under the antialias ramp, and 88 is far enough past a usable oblique view.
        _autoFlattenParallax(2.0f),
        _autoFlattenTilt(88.0f),
        _autoFlattenDuration(0.3f),
        _autoFlattenRiseDuration(-1.0f), // negative: the same as the flattening one

        // 64 triangles per tile side, which is what tangram uses (RasterStyle::build) and what every
        // bench here was run at. 32 leaves draped content visibly floating over the ground; 128
        // measured 8.5 fps against 15.2 at 64 on the Crosscall.
        _meshResolution(64),
        // 0: the node field follows the mesh, which is what every caller got before it could be
        // asked for separately. See setSurfaceNodeResolution for why the two are worth splitting.
        _surfaceNodeResolution(0),
        _postProcessDownscale(2),
        _tileEdgeStitchingEnabled(true),
        _meshCacheSize(0),
        _sharedGroundEnabled(true),
        _drapeFillsEnabled(true),
        _drapeLinesEnabled(true),
        _bridges3DEnabled(false),
        _drapeResolution(0),
        _minZoom(5),
        _maxZoom(0),
        _maxTileZoomOffset(100),
        _backgroundColorARGB(0),
        _backgroundBitmapEnabled(false),
        _depthBias(0.0002f),
        // 60 m, not 200: 200 stops the camera well short of the surface, so a close approach swings
        // the view into the nearest hillside instead of flying between the peaks.
        _cameraClearance(0.0f),
        _cameraClearanceFraction(static_cast<float>(CameraClearance::FRACTION)),
        _focusLift(0.0f),
        _cameraClampDuration(0.0f),
        _billboardOcclusionEnabled(true),
        // 0.2, not 0: measured at Grenoble from the south of La Bastille, where 0 dropped POIs on
        // slopes FACING the camera. The grazing term in TerrainOcclusion::isBehind covers the angle;
        // this covers what is left - the anchor-vs-drawn-surface error itself.
        _billboardOcclusionTolerance(0.2f),
        _normalSampleDistance(0.0f),
        _textOcclusionOpacity(1.0f),
        _viewDistanceFactor(1.0f),
        _viewDistance(0.0f),
        _viewDistanceMax(0.0f),
        // 3, not the demo's 8: 8 only pays for itself next to the demo's fixed 170 km view. On the
        // default view distance it coarsens tiles that are still large on screen, leaving a blurred
        // band with a hard tile edge down the middle.
        _drapeCacheSize(0),
        _elevationCacheSize(0),
        _drapeWorkingSet(0),
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
        // use_count AFTER this member is the number of OUTSIDE holders: 1 means only this, so the
        // manager dies with us. More means something else pinned it - TerrainTileTransformer and
        // TerrainProjectionSurface both keep a const shared_ptr, and Options::setTerrainOptions(null)
        // touches neither.
        Log::Infof("LIFE: TerrainOptions destroyed, elevationManager use_count=%ld", static_cast<long>(_elevationManager.use_count()));
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

    bool TerrainOptions::isActive() const {
        return _enabled.load() && _flattenRatio.load() < 1.0f;
    }

    bool TerrainOptions::isFlattened() const {
        return _flattened.load();
    }

    void TerrainOptions::setFlattened(bool flattened) {
        bool wasManual = _flattenManual.exchange(false); // asking for a state hands the ratio back
        if (_flattened.exchange(flattened) != flattened || wasManual) {
            // Before the renderer's switch has ever run, this IS the state: an app starting in 2D
            // sets it before its layers exist and must not decode a tile for 3D first. From the first
            // frame on the switch owns both, and moves the decode only while the map is flat.
            if (!_flattenSwitchStarted.load()) {
                writeFlattenRatio(flattened ? 1.0f : 0.0f);
                if (_flattenMode.load() == TerrainFlattenMode::TERRAIN_FLATTEN_MODE_FULL) {
                    _decodeActive.store(!flattened);
                }
            }
            markSwitchingIfRising(flattened ? 1.0f : 0.0f);
            notifyOptionChanged("Flattened");
        }
    }

    TerrainFlattenMode::TerrainFlattenMode TerrainOptions::getFlattenMode() const {
        return _flattenMode.load();
    }

    void TerrainOptions::setFlattenMode(TerrainFlattenMode::TerrainFlattenMode mode) {
        if (_flattenMode.exchange(mode) != mode) {
            if (mode == TerrainFlattenMode::TERRAIN_FLATTEN_MODE_RENDER) {
                _decodeActive.store(true); // RENDER decodes for 3D whatever the flatten state
            }
            notifyOptionChanged("FlattenMode");
        }
    }

    bool TerrainOptions::isDecodeActive() const {
        return _enabled.load() && _decodeActive.load();
    }

    void TerrainOptions::setDecodeActive(bool active) {
        if (_decodeActive.exchange(active) != active) {
            notifyOptionChanged("DecodeActive");
        }
    }

    float TerrainOptions::getFlattenRatio() const {
        return _flattenRatio.load();
    }

    void TerrainOptions::setFlattenRatio(float ratio) {
        float value = std::min(1.0f, std::max(0.0f, ratio));
        _flattenManualRatio.store(value);
        _flattenManual.store(true);
        // Flattened is the state the app can read back, so keep it honest while the app drives.
        _flattened.store(value >= 1.0f);
        if (!_flattenSwitchStarted.load()) {
            writeFlattenRatio(value); // no frame has run yet; the switch seeds itself from this
        }
        markSwitchingIfRising(value);
        notifyOptionChanged("FlattenRatio");
    }

    void TerrainOptions::markSwitchingIfRising(float askedRatio) {
        // Asking for 3D off a flat map WILL wait for tiles, and the renderer only says so on its
        // next frame. An app that polls isSwitching() before that frame reads false, starts its
        // flight against a ground the switch then holds flat, and the terrain ramps after it lands.
        if (askedRatio < 1.0f && _flattenRatio.load() >= 1.0f && _flattenSwitchStarted.load()) {
            _switching.store(true);
        }
    }

    bool TerrainOptions::isManualFlatten() const {
        return _flattenManual.load();
    }

    float TerrainOptions::getManualFlattenRatio() const {
        return _flattenManualRatio.load();
    }

    bool TerrainOptions::isSwitching() const {
        return _switching.load();
    }

    void TerrainOptions::setSwitching(bool switching) {
        _switching.store(switching);
    }

    void TerrainOptions::applyFlattenRatio(float ratio) {
        _flattenSwitchStarted.store(true); // only the renderer's switch calls this
        writeFlattenRatio(ratio);
    }

    void TerrainOptions::writeFlattenRatio(float ratio) {
        float value = std::min(1.0f, std::max(0.0f, ratio));
        if (_flattenRatio.exchange(value) != value) {
            _elevationManager->setExaggeration(_exaggeration.load() * (1.0f - value));
        }
    }

    float TerrainOptions::getAutoFlattenRiseDuration() const {
        return _autoFlattenRiseDuration.load();
    }

    void TerrainOptions::setAutoFlattenRiseDuration(float duration) {
        if (_autoFlattenRiseDuration.exchange(duration) != duration) {
            notifyOptionChanged("AutoFlattenRiseDuration");
        }
    }

    float TerrainOptions::getAutoFlattenParallax() const {
        return _autoFlattenParallax.load();
    }

    void TerrainOptions::setAutoFlattenParallax(float pixels) {
        float value = std::max(0.0f, pixels);
        if (_autoFlattenParallax.exchange(value) != value) {
            notifyOptionChanged("AutoFlattenParallax");
        }
    }

    float TerrainOptions::getAutoFlattenTilt() const {
        return _autoFlattenTilt.load();
    }

    void TerrainOptions::setAutoFlattenTilt(float tilt) {
        float value = std::min(90.0f, std::max(0.0f, tilt));
        if (_autoFlattenTilt.exchange(value) != value) {
            notifyOptionChanged("AutoFlattenTilt");
        }
    }

    float TerrainOptions::getAutoFlattenDuration() const {
        return _autoFlattenDuration.load();
    }

    void TerrainOptions::setAutoFlattenDuration(float duration) {
        float value = std::max(0.0f, duration);
        if (_autoFlattenDuration.exchange(value) != value) {
            notifyOptionChanged("AutoFlattenDuration");
        }
    }

    float TerrainOptions::getExaggeration() const {
        return _exaggeration.load();
    }

    void TerrainOptions::setExaggeration(float exaggeration) {
        if (_exaggeration.exchange(exaggeration) != exaggeration) {
            _elevationManager->setExaggeration(exaggeration * (1.0f - _flattenRatio.load()));
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

    int TerrainOptions::getSurfaceNodeResolution() const {
        return _surfaceNodeResolution.load();
    }

    void TerrainOptions::setSurfaceNodeResolution(int resolution) {
        int value = (resolution <= 0 ? 0 : std::min(512, std::max(2, resolution)));
        if (_surfaceNodeResolution.exchange(value) != value) {
            notifyOptionChanged("SurfaceNodeResolution");
        }
    }

    int TerrainOptions::getPostProcessDownscale() const {
        return _postProcessDownscale.load();
    }

    void TerrainOptions::setPostProcessDownscale(int downscale) {
        int scale = std::min(4, std::max(1, downscale));
        if (_postProcessDownscale.exchange(scale) != scale) {
            notifyOptionChanged("PostProcessDownscale");
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

    int TerrainOptions::getMeshCacheSize() const {
        return _meshCacheSize.load();
    }

    void TerrainOptions::setMeshCacheSize(int meshes) {
        int clamped = std::max(0, meshes);
        if (_meshCacheSize.exchange(clamped) != clamped) {
            notifyOptionChanged("MeshCacheSize");
        }
    }

    bool TerrainOptions::isSharedGroundEnabled() const {
        return _sharedGroundEnabled.load();
    }

    void TerrainOptions::setSharedGroundEnabled(bool enabled) {
        if (_sharedGroundEnabled.exchange(enabled) != enabled) {
            notifyOptionChanged("SharedGroundEnabled");
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

    bool TerrainOptions::isBridges3DEnabled() const {
        return _bridges3DEnabled.load();
    }

    void TerrainOptions::setBridges3DEnabled(bool enabled) {
        if (_bridges3DEnabled.exchange(enabled) != enabled) {
            notifyOptionChanged("Bridges3DEnabled");
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

    int TerrainOptions::getMaxZoom() const {
        return _maxZoom.load();
    }

    void TerrainOptions::setMaxZoom(int maxZoom) {
        int zoom = std::min(24, std::max(0, maxZoom));
        if (_maxZoom.exchange(zoom) != zoom) {
            // The DATA too, not only the mesh cut. Pinning the cut alone measured no better: the
            // cut stopped moving (RenderStats tileRecalc 0) while the elevation grid cache kept
            // thrashing at capacity (elevGrid reinserts 2-6 per second, indefinitely), and every
            // reload bumps the elevation version - so labels went on re-anchoring and the ground
            // went on moving. See ElevationManager::setMaxDataZoomCap.
            if (_elevationManager) {
                _elevationManager->setMaxDataZoomCap(zoom);
            }
            notifyOptionChanged("MaxZoom");
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

    int TerrainOptions::getDrapeCacheSize() const {
        return _drapeCacheSize.load();
    }

    void TerrainOptions::setDrapeCacheSize(int megabytes) {
        int clamped = std::max(0, megabytes);
        if (_drapeCacheSize.exchange(clamped) != clamped) {
            notifyOptionChanged("DrapeCacheSize");
        }
    }

    int TerrainOptions::getElevationCacheSize() const {
        return _elevationCacheSize.load();
    }

    void TerrainOptions::setElevationCacheSize(int megabytes) {
        int clamped = std::max(0, megabytes);
        if (_elevationCacheSize.exchange(clamped) != clamped) {
            // Straight through to the manager, like SetMaxZoom does for the data zoom cap: the
            // budget belongs to the grid cache, and the manager is the only thing that owns one.
            // ONLY when asked for: setCacheCapacity latches _gridCacheCapacityFixed, so passing 0
            // through would pin the cache at zero bytes rather than restore the grid-count rule.
            // 0 here therefore means "never told the manager anything", which is what the default is.
            if (_elevationManager && clamped > 0) {
                _elevationManager->setCacheCapacity(static_cast<std::size_t>(clamped) * 1024 * 1024);
            }
            notifyOptionChanged("ElevationCacheSize");
        }
    }

    int TerrainOptions::getDrapeWorkingSet() const {
        return _drapeWorkingSet.load();
    }

    void TerrainOptions::setDrapeWorkingSet(int tiles) {
        int clamped = std::max(0, tiles);
        if (_drapeWorkingSet.exchange(clamped) != clamped) {
            notifyOptionChanged("DrapeWorkingSet");
        }
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

    float TerrainOptions::getViewDistanceMax() const {
        return _viewDistanceMax.load();
    }

    void TerrainOptions::setViewDistanceMax(float distance) {
        float clamped = std::max(0.0f, distance);
        if (_viewDistanceMax.exchange(clamped) != clamped) {
            notifyOptionChanged("ViewDistanceMax");
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

    float TerrainOptions::getCameraClearanceFraction() const {
        return _cameraClearanceFraction.load();
    }

    void TerrainOptions::setCameraClearanceFraction(float fraction) {
        // Below 1 strictly: the shell divides by (1 - fraction), and at 1 no camera height clears it.
        float value = std::min(0.99f, std::max(0.0f, fraction));
        if (_cameraClearanceFraction.exchange(value) != value) {
            notifyOptionChanged("CameraClearanceFraction");
        }
    }

    float TerrainOptions::getFocusLift() const {
        return _focusLift.load();
    }

    void TerrainOptions::setFocusLift(float lift) {
        float value = std::max(0.0f, lift);
        if (_focusLift.exchange(value) != value) {
            notifyOptionChanged("FocusLift");
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

    float TerrainOptions::getNormalSampleDistance() const {
        return _normalSampleDistance.load();
    }

    void TerrainOptions::setNormalSampleDistance(float distance) {
        float value = std::max(0.0f, distance);
        if (_normalSampleDistance.exchange(value) != value) {
            notifyOptionChanged("NormalSampleDistance");
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

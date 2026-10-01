#include "TileRenderer.h"

#include <vt/LabelFade.h>
#include <vt/RenderStats.h>
#include "components/Options.h"
#include "components/DayCycleLight.h"
#include "components/LightOptions.h"
#include "components/TerrainOptions.h"
#include "components/FogOptions.h"
#include "components/ThreadWorker.h"
#include "graphics/ViewState.h"
#include "projections/ProjectionSurface.h"
#include "projections/PlanarProjectionSurface.h"
#include "renderers/MapRenderer.h"
#include "renderers/drawdatas/TileDrawData.h"
#include "renderers/TerrainRenderer.h"
#include "renderers/utils/ElevationTextureCache.h"
#include "renderers/utils/FogShader.h"
#include "renderers/utils/GLResourceManager.h"
#include "renderers/utils/TerrainDrapeCache.h"
#include "renderers/utils/VTRenderer.h"
#include "layers/HillshadeRasterTileLayer.h"
#include "terrain/DrapeTuning.h"
#include "terrain/ElevationManager.h"
#include "utils/Const.h"
#include "utils/Log.h"

#ifdef __ANDROID__
#include <sys/system_properties.h>
#include <cstdlib>
#endif
#include "utils/Const.h"

#include <limits>
#include <atomic>
#include <chrono>

#include <vt/Label.h>
#include <vt/LabelCuller.h>
#include <vt/TileTransformer.h>
#include <vt/GLExtensions.h>
#include <vt/NormalMapBuilder.h>

#include <cmath>
#include <cstring>
#include <unordered_map>

#include <cglib/mat.h>

namespace massif {

    struct TileRenderer::LabelOcclusionState {
        std::mutex mutex;
        cglib::vec3<double> cameraPos = cglib::vec3<double>(0, 0, 0);
        unsigned int elevationVersion = 0;
        std::unordered_map<long long, bool> results;
    };

    TileRenderer::TileRenderer() :
        _mapRenderer(),
        _options(),
        _tileTransformer(),
        _vtRenderer(),
        _interactionMode(false),
        _layerBlendingSpeed(1.0f),
        _labelBlendingSpeed(vt::DEFAULT_LABEL_BLENDING_SPEED),
        _labelPerspectiveScaling(0.5f),
        _labelOrder(0),
        _buildingOrder(1),
        _rasterFilterMode(vt::RasterFilterMode::BILINEAR),
        _normalMapLightingShader(LIGHTING_SHADER_NORMALMAP),
        _normalMapShadowColor(0, 0, 0, 255),
        _normalMapAccentColor(0, 0, 0, 255),
        _normalMapHighlightColor(255, 255, 255, 255),
        _rendererLayerFilter(),
        _clickHandlerLayerFilter(),
        _horizontalLayerOffset(0),
        _viewDir(0, 0, 0),
        _normalLightDir(0, 0, 0),
        _normalIlluminationMapRotationEnabled(false),
        _normalIlluminationDirection(0,0,0),
        _mapRotation(0),
        _hillshadeMethod(HillshadeMethod::STANDARD),
        _hillshadeExaggeration(1.0f),
        _hillshadeIntensity(0.5f),
        _tiles(),
        _mutex()
    {
    }
    
    TileRenderer::~TileRenderer() {
    }
    
    void TileRenderer::setComponents(const std::weak_ptr<Options>& options, const std::weak_ptr<MapRenderer>& mapRenderer) {
        std::lock_guard<std::mutex> lock(_mutex);
        _options = options;
        _mapRenderer = mapRenderer;
        _vtRenderer.reset();
    }

    std::shared_ptr<vt::TileTransformer> TileRenderer::getTileTransformer() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tileTransformer;
    }

    void TileRenderer::setTileTransformer(const std::shared_ptr<vt::TileTransformer>& tileTransformer) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_tileTransformer != tileTransformer) {
            _vtRenderer.reset();
        }
        _tileTransformer = tileTransformer;
    }
    
    void TileRenderer::setInteractionMode(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);
        _interactionMode = enabled;
    }

    // Timed: what a cull-thread tile-set change costs the frame; refreshTilesLockNs is the other side.
    std::unique_lock<std::mutex> TileRenderer::lockTimed() const {
        VT_STAT_CLOCK(lockClock);
        std::unique_lock<std::mutex> lock(_mutex);
        VT_STAT_SPLIT(tileRendererLockNs, lockClock);
        return lock;
    }

    void TileRenderer::setTerrainRenderOrder(int order) {
        auto lock = lockTimed();
        _terrainRenderOrder = order;
    }

    void TileRenderer::setTerrainDepthWriteMode(bool enabled) {
        auto lock = lockTimed();
        _terrainDepthWriteMode = enabled;
    }
    
    void TileRenderer::setLayerBlendingSpeed(float speed) {
        std::lock_guard<std::mutex> lock(_mutex);
        _layerBlendingSpeed = speed;
    }

    void TileRenderer::setLabelBlendingSpeed(float speed) {
        std::lock_guard<std::mutex> lock(_mutex);
        _labelBlendingSpeed = speed;
    }

    void TileRenderer::setLabelPerspectiveScaling(float scaling) {
        std::lock_guard<std::mutex> lock(_mutex);
        _labelPerspectiveScaling = scaling;
    }

    void TileRenderer::setLabelOrder(int order) {
        std::lock_guard<std::mutex> lock(_mutex);
        _labelOrder = order;
    }
    
    void TileRenderer::setBuildingOrder(int order) {
        std::lock_guard<std::mutex> lock(_mutex);
        _buildingOrder = order;
    }

    void TileRenderer::setRasterFilterMode(vt::RasterFilterMode filterMode) {
        std::lock_guard<std::mutex> lock(_mutex);
        _rasterFilterMode = filterMode;
    }

    void TileRenderer::setNormalMapShadowColor(const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);
        _normalMapShadowColor = color;
    }

    void TileRenderer::setNormalMapHighlightColor(const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);
        _normalMapHighlightColor = color;
    }
    void TileRenderer::setNormalMapAccentColor(const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);
        _normalMapAccentColor = color;
    }
    void TileRenderer::setNormalMapLightingShader(const std::string& shader) {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string newValue = shader;
        if (newValue.length() == 0) {
            newValue = LIGHTING_SHADER_NORMALMAP;
        }
        if (newValue != _normalMapLightingShader) {
            _normalMapLightingShader = newValue;
            // Swapped in place: a reset threw away the layer's whole GL renderer, and until every tile
            // was uploaded again the map drew without it - black on Mali. Called on the GL thread.
            if (_vtRenderer && _vtRenderer->isValid()) {
                if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer()) {
                    tileRenderer->setLightingShaderNormalMap(createNormalMapLightingShader());
                }
            }
        }
    }
    void TileRenderer::setNormalMapElevationEncoded(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);
        _normalMapElevationEncoded = enabled;
    }
    void TileRenderer::setNormalMapContourInterval(float interval) {
        std::lock_guard<std::mutex> lock(_mutex);
        _normalMapContourInterval = interval;
    }
    void TileRenderer::setNormalMapContourColor(const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);
        _normalMapContourColor = color;
    }
    void TileRenderer::setNormalMapContourWidth(float width) {
        std::lock_guard<std::mutex> lock(_mutex);
        _normalMapContourWidth = width;
    }
    void TileRenderer::setNormalIlluminationDirection(MapVec direction) {
        std::lock_guard<std::mutex> lock(_mutex);
        _normalIlluminationDirection = direction;
    }

    void TileRenderer::setNormalIlluminationMapRotationEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);
        _normalIlluminationMapRotationEnabled = enabled;
    }

    void TileRenderer::setHillshadeMethod(int method) {
        std::lock_guard<std::mutex> lock(_mutex);
        _hillshadeMethod = method;
    }

    void TileRenderer::setHillshadeExaggeration(float exaggeration) {
        std::lock_guard<std::mutex> lock(_mutex);
        _hillshadeExaggeration = exaggeration;
    }

    void TileRenderer::setHillshadeIntensity(float intensity) {
        std::lock_guard<std::mutex> lock(_mutex);
        _hillshadeIntensity = intensity;
    }

    void TileRenderer::setRendererLayerFilter(const std::optional<std::regex>& filter) {
        std::lock_guard<std::mutex> lock(_mutex);
        _rendererLayerFilter = filter;
    }

    void TileRenderer::setClickHandlerLayerFilter(const std::optional<std::regex>& filter) {
        std::lock_guard<std::mutex> lock(_mutex);
        _clickHandlerLayerFilter = filter;
    }

    void TileRenderer::offsetLayerHorizontally(double offset) {
        std::lock_guard<std::mutex> lock(_mutex);
        _horizontalLayerOffset += offset;
    }
    
    /**
     * State renderDrapedSurface needs, pushed from prepareFrame too: the shared ground draws before
     * onDrawFrame, and the first 3D frame had no ground. Caller must hold _mutex.
     */
    void TileRenderer::pushTerrainDrapeState() {
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>());
        if (!tileRenderer) {
            return;
        }
        bool terrainMode = false;
        std::shared_ptr<TerrainOptions> activeTerrainOptions;
        if (auto options = _options.lock()) {
            if (auto terrainOptions = options->getTerrainOptions()) {
                if (terrainOptions->isActive()) {
                    terrainMode = true;
                    activeTerrainOptions = terrainOptions;
                }
            }
        }
        // An already built cache only: onDrawFrame creates it and pushes the authoritative values later.
        std::shared_ptr<ElevationTextureCache> elevationTextureCache;
        if (terrainMode) {
            elevationTextureCache = _elevationTextureCache;
        }
        vt::GLTileRenderer::TerrainTextureProvider terrainTextureProvider;
        if (elevationTextureCache) {
            terrainTextureProvider = [elevationTextureCache](const vt::TileId& tileId, vt::GLTileRenderer::TerrainTexture& terrainTexture) {
                return elevationTextureCache->getTexture(tileId, terrainTexture);
            };
        }
        float terrainDepthBias = 0.0f;
        if (terrainMode && !terrainTextureProvider) {
            terrainDepthBias = activeTerrainOptions->getDepthBias() * 0.1f;
        }
        tileRenderer->setTerrainTextureProvider(terrainTextureProvider);
        tileRenderer->setTerrainMode(terrainMode, terrainDepthBias);
        tileRenderer->setTerrainRegularGrid(terrainMode && (bool) terrainTextureProvider,
                                            activeTerrainOptions ? activeTerrainOptions->getMeshResolution() : 0);
    }

    bool TileRenderer::prepareFrame(float deltaSeconds, const ViewState& viewState) {
        auto lock = lockTimed();

        return prepareFrameUnsafe(deltaSeconds, viewState);
    }

    // Caller must hold _mutex (not recursive).
    bool TileRenderer::prepareFrameUnsafe(float deltaSeconds, const ViewState& viewState) {
        if (_framePrepared) {
            return _framePrepareResult;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>());
        if (!tileRenderer) {
            return false;
        }
        _framePrepared = true;
        _framePrepareResult = false;
        // Resolved here: the drape bake runs before onDrawFrame, and is never re-baked for a uniform change.
        if (auto options = _options.lock()) {
            ResolvedLighting lighting = resolveLighting(options->getLightOptions(), _styleEnvironment);
            _groundAOIntensity = lighting.buildingAoIntensity;
            _groundAOAttenuation = lighting.buildingAoGroundAttenuation;
            _resolvedRadiance = lighting.radiance;
            _resolvedBrightness = lighting.brightness;
            _backgroundEmissive = lighting.backgroundEmissive;
            _buildingHeightScale = lighting.buildingHeightScale;
        _buildingHeightViewScale = lighting.buildingHeightViewScale;
            _buildingGrowOnAppear = lighting.buildingGrowOnAppear;
            _buildingFadeOnAppear = lighting.buildingFadeOnAppear;
            // The owner reads this before the layer passes, to decide on the occluder buffer.
            _textOcclusionOpacity.store(resolveTextOcclusionOpacity(options->getTerrainOptions(), _styleEnvironment));
        }
        // The shared surface draws before onDrawFrame sets the view state, or the ground lags a frame.
        cglib::mat4x4<double> prepareModelViewMat = viewState.getModelviewMat() * cglib::translate4_matrix(cglib::vec3<double>(_horizontalLayerOffset, 0, 0));
        vt::ViewState prepareViewState(viewState.getProjectionMat(), prepareModelViewMat, viewState.getRenderZoom(), viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        // vt scales labels by the planar WORLD_SIZE; the globe's world is twice as wide.
        prepareViewState.zoomScale *= static_cast<float>(viewState.worldPerInternal());
        prepareViewState.styleZoomShift = _styleZoomShift.load();
        prepareViewState.planarProjection = isPlanarProjectionMode();
        prepareViewState.labelPerspectiveScaling = _labelPerspectiveScaling;
        prepareViewState.lightBrightness = _resolvedBrightness;
        // vt's fallback (camera height above z=0) is wrong on a globe.
        prepareViewState.focusDistance = static_cast<float>(cglib::length(viewState.getCameraPos() - viewState.getFocusPos()));
        // For screen-space objects not sized from the zoom; setLineAntialiasScale needs the same ratio.
        prepareViewState.deviceResolution = static_cast<float>(viewState.getHeight());
        // Label::updatePlacement needs it, or distant labels stay unplaced.
        if (auto options = _options.lock()) {
            prepareViewState.labelViewDistance = options->getLabelViewDistance();
        }
        tileRenderer->setViewState(prepareViewState);
        tileRenderer->setGroundAO(_groundAOIntensity, _groundAOAttenuation);
        tileRenderer->setRadiance(_resolvedRadiance);
        tileRenderer->setBackgroundEmissive(_backgroundEmissive);
        tileRenderer->setBuildingHeight(_buildingHeightScale, _buildingHeightViewScale, _buildingGrowOnAppear, _buildingFadeOnAppear);
        tileRenderer->setLabelOcclusionOpacity(_textOcclusionOpacity.load());
        pushTerrainDrapeState();
        try {
            _framePrepareResult = tileRenderer->startFrame(deltaSeconds * 3);
            // Re-anchored labels were placed at their old height; a still camera asks for no new pass.
            if (tileRenderer->consumeLabelsReanchored()) {
                _labelPlacementOwed = true;
            }
        }
        catch (const std::exception& ex) {
            Log::Errorf("TileRenderer::prepareFrame: Failed: %s", ex.what());
        }
        return _framePrepareResult;
    }

    void TileRenderer::setExternalDrapeTarget(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _externalDrapeTarget = enabled;
        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setExternalDrapeTarget(enabled);
        }
    }

    void TileRenderer::setExternalDrapeTiles(const std::vector<vt::TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setExternalDrapeTiles(tileIds);
        }
    }

    int TileRenderer::resolveDrapeResolution(int setting, const ViewState& viewState, const std::shared_ptr<Options>& options, std::size_t budgetMegabytes, int workingSet) {
        if (setting > 0) {
            return setting;
        }
        // From the screen: a tile is at most 2 * tileDrawSize * pixelScale wide, one texel per pixel there.
        double tileDrawSize = (options ? options->getTileDrawSize() : 256);
        // Then capped by memory, the binding constraint (TerrainOptions::DrapeCacheSize / DrapeWorkingSet).
        std::size_t budget = (budgetMegabytes > 0 ? budgetMegabytes * 1024 * 1024 : TerrainDrapeCache::MAX_BYTES);
        std::size_t budgetBytes = (TerrainDrapeCache::isBudgetEnabled() ? budget : 0);
        std::size_t tiles = (workingSet > 0 ? static_cast<std::size_t>(workingSet) : DRAPE_WORKING_SET);
        return DrapeTuning::resolution(tileDrawSize, viewState.getDPI() / Const::UNSCALED_DPI, tiles, budgetBytes, MIN_DRAPE_RESOLUTION, MAX_DRAPE_RESOLUTION);
    }

    int TileRenderer::getStyleLayerCount() const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->getStyleLayerCount();
        }
        return 0;
    }

    void TileRenderer::setTerrainLayerOrdinalBase(int base) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setTerrainLayerOrdinalBase(base);
        }
    }

    void TileRenderer::setTerrainGroundTiles(const std::vector<vt::TileId>& tileIds, const std::vector<int>& proxyDepths) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainGroundActive = !tileIds.empty();
        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setTerrainGroundTiles(tileIds, proxyDepths);
        }
    }

    int TileRenderer::renderTerrainGround(const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->renderTerrainGround(vt::Color(color.getR() / 255.0f, color.getG() / 255.0f, color.getB() / 255.0f, color.getA() / 255.0f));
        }
        return 0;
    }

    void TileRenderer::collectDrapeTiles(std::map<vt::TileId, std::size_t>& drapeTiles) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->collectDrapeTiles(drapeTiles);
        }
    }

    int TileRenderer::bakeDrapeTile(const vt::TileId& tileId) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->bakeDrapeTile(tileId);
        }
        return 0;
    }

    void TileRenderer::collectSpanDrapeTiles(std::map<vt::TileId, std::size_t>& spanTiles) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->collectSpanDrapeTiles(spanTiles);
        }
    }

    void TileRenderer::collectUnresolvedSpanEnds(std::vector<std::pair<int, cglib::vec2<double>>>& ends) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->collectUnresolvedSpanEnds(ends);
        }
    }

    int TileRenderer::bakeSpanDrapeTile(const vt::TileId& tileId) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->bakeSpanDrapeTile(tileId);
        }
        return 0;
    }

    void TileRenderer::setSpanDrapeTextures(const std::map<vt::TileId, unsigned int>& textures) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setSpanDrapeTextures(textures);
        }
    }

    void TileRenderer::setGroundDrapeTextures(const std::map<vt::TileId, vt::GLTileRenderer::GroundDrape>& drapes) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setGroundDrapeTextures(drapes);
        }
    }

    void TileRenderer::collectDrapeStackOrder(std::vector<std::pair<int, bool> >& units) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->collectDrapeStackOrder(units);
        }
    }

    int TileRenderer::bakeDrapeCoverage(const vt::TileId& tileId, int fromStyleLayerIdx) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->bakeDrapeCoverage(tileId, fromStyleLayerIdx);
        }
        return 0;
    }

    void TileRenderer::setDrapeCoverageMasks(const std::vector<std::map<vt::TileId, unsigned int> >& maskTextures, const std::map<int, int>& styleLayerMasks) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setDrapeCoverageMasks(maskTextures, styleLayerMasks);
        }
    }

    int TileRenderer::renderDrapedSurface(const vt::TileId& tileId, unsigned int drapeTexture, float uvOffsetX, float uvOffsetY, float uvScale) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->renderDrapedSurface(tileId, static_cast<GLuint>(drapeTexture), uvOffsetX, uvOffsetY, uvScale);
        }
        return -4;
    }

    int TileRenderer::blitDrapeTexture(unsigned int srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->blitDrapeTexture(static_cast<GLuint>(srcTexture), dstOffsetX, dstOffsetY, dstScale, uvOffsetX, uvOffsetY, uvScale);
        }
        return -4;
    }

    int TileRenderer::renderDrapedSurfaceFill(const vt::TileId& tileId, const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->renderDrapedSurfaceFill(tileId, vt::Color(color.getR() / 255.0f, color.getG() / 255.0f, color.getB() / 255.0f, color.getA() / 255.0f));
        }
        return -4;
    }

    bool TileRenderer::calculateShadowViewProj(const std::vector<vt::TileId>& tileIds, const std::vector<vt::TileId>& casterTileIds, const std::vector<std::pair<double, double> >& casterHeights, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<vt::TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            // Fallback when no tile carries a DEM, or a 2D map's fit fails and draws no shadow.
            tileRenderer->setMetersToInternal(Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE);
            return tileRenderer->calculateShadowViewProj(tileIds, casterTileIds, casterHeights, sunDir, tileHeights, minHeight, maxHeight, distanceFactor, cameraDistance, mapSize, cascade, cascadeCount, boxCasterTileIds, depthRangeMeters, texelMeters, lightViewProj);
        }
        return false;
    }

    float TileRenderer::shadowCasterFadeSignature(const std::vector<vt::TileId>* coveredBy) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->shadowCasterFadeSignature(coveredBy);
        }
        return 0.0f;
    }

    int TileRenderer::consumeShadowCastersMissingElevation() {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->consumeShadowCastersMissingElevation();
        }
        return 0;
    }

    int TileRenderer::renderShadowCasters(const std::vector<vt::TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->renderShadowCasters(tileIds, lightViewProj, castGround);
        }
        return 0;
    }

    void TileRenderer::setTerrainShadowMask(unsigned int texture, float invScreenWidth, float invScreenHeight) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setTerrainShadowMask(static_cast<GLuint>(texture), invScreenWidth, invScreenHeight);
        }
    }

    int TileRenderer::renderTerrainShadowMask(const std::vector<vt::TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->renderTerrainShadowMask(tileIds);
        }
        return 0;
    }

    bool TileRenderer::isGroundAOActive() const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->isGroundAOActive();
        }
        return false;
    }

    bool TileRenderer::hasGroundContent() const {
        auto lock = lockTimed();

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->hasGroundContent();
        }
        return false;
    }

    bool TileRenderer::isGroundAOBakeable() const {
        auto lock = lockTimed();

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->isGroundAOBakeable();
        }
        return false;
    }

    int TileRenderer::renderGroundAOMask() {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->renderGroundAOMask();
        }
        return 0;
    }

    int TileRenderer::bakeGroundAOMask(const vt::TileId& tileId) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->bakeGroundAOMask(tileId);
        }
        return 0;
    }

    void TileRenderer::setTerrainShadowMap(unsigned int texture, int mapSize, int cascades, const cglib::vec3<float>& depthBias, const std::array<float, 4>& depthScales, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec2<float>& fadeRange, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, 4>& lightViewProjs) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setTerrainShadowMap(static_cast<GLuint>(texture), mapSize, cascades, depthBias, depthScales, strength, softness, depthTexture, hardwarePCF, normalOffset, fadeRange, sunDir, lightViewProjs);
        }
    }

    cglib::vec3<float> TileRenderer::linearColor(const Color& color, float intensity) {
        auto linear = [intensity](unsigned char c) { return std::pow(c / 255.0f, 2.2f) * intensity; };
        return cglib::vec3<float>(linear(color.getR()), linear(color.getG()), linear(color.getB()));
    }

    vt::GLTileRenderer::TerrainLighting TileRenderer::buildTerrainLighting(const ResolvedLighting& lighting) {
        vt::GLTileRenderer::TerrainLighting terrainLighting;
        terrainLighting.enabled = true;
        // Prelit colours get neutral light, not none: the shadow multiply lives in the shading block.
        if (lighting.colorsPrelit) {
            terrainLighting.sunDir = lighting.sunDir;
            terrainLighting.sunColor = cglib::vec3<float>(1.0f, 1.0f, 1.0f);
            terrainLighting.ambientColor = cglib::vec3<float>(1.0f, 1.0f, 1.0f);
            terrainLighting.sunIntensity = 0.0f;
            terrainLighting.ambientIntensity = 1.0f;
            return terrainLighting;
        }
        terrainLighting.sunDir = lighting.sunDir;
        terrainLighting.sunColor = cglib::vec3<float>(lighting.sunColor.getR() / 255.0f, lighting.sunColor.getG() / 255.0f, lighting.sunColor.getB() / 255.0f);
        terrainLighting.ambientColor = cglib::vec3<float>(lighting.ambientColor.getR() / 255.0f, lighting.ambientColor.getG() / 255.0f, lighting.ambientColor.getB() / 255.0f);
        terrainLighting.sunIntensity = lighting.sunIntensity;
        terrainLighting.ambientIntensity = lighting.ambientIntensity;
        return terrainLighting;
    }

    void TileRenderer::setTerrainSunLighting(const ResolvedLighting& lighting) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            vt::GLTileRenderer::TerrainLighting terrainLighting;
            if (lighting.terrainLightingEnabled) {
                terrainLighting = buildTerrainLighting(lighting);
            }
            tileRenderer->setRadiance(_resolvedRadiance);
            tileRenderer->setBackgroundEmissive(_backgroundEmissive);
            // Fallback emissive, restored after a rule that overrides u_emissive.
            tileRenderer->setBuildingEmissive(_buildingEmissive);
            tileRenderer->setTerrainLighting(terrainLighting);
        }
    }

    void TileRenderer::setTerrainPaintTiles(const std::vector<vt::TileId>& tileIds) {
        auto lock = lockTimed();

        // The push takes the vt mutex, which a tile-set change holds for a whole label map rebuild.
        if (tileIds == _terrainPaintTileIds) {
            return;
        }
        _terrainPaintTileIds = tileIds;
        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setTerrainPaintTiles(tileIds);
        }
    }

    void TileRenderer::setTerrainPaint(bool enabled, bool fullDetail, float heightScale, bool exaggerateHeightScale, bool legacyHeightScale, float contrast, float opacity, std::size_t fingerprint) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainPaintEnabled = enabled;
        _terrainPaintFullDetail = fullDetail;
        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            vt::GLTileRenderer::TerrainPaint paint;
            paint.enabled = enabled;
            paint.heightScale = heightScale;
            paint.exaggerateHeightScale = exaggerateHeightScale;
            paint.legacyHeightScale = legacyHeightScale;
            paint.contrast = contrast;
            paint.opacity = opacity;
            paint.fingerprint = fingerprint;
            tileRenderer->setTerrainPaint(paint);
            tileRenderer->setTerrainPaintOnGround(isTerrainPaintOnGroundForced());
            tileRenderer->setTerrainDemTaps(terrainDemTaps());
            tileRenderer->setTerrainTileBackgrounds(isTerrainTileBackgroundsForced());
        }
    }

    // Measurement switches (demo builds): debug.massif.groundpaint, demtaps (16/4/1), tilebg.
#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
    bool TileRenderer::isTerrainTileBackgroundsForced() {
        static const bool forced = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return __system_property_get("debug.massif.tilebg", property) > 0 && property[0] == '1';
        }();
        return forced;
    }
#else
    bool TileRenderer::isTerrainTileBackgroundsForced() {
        return false;
    }
#endif

    // Stencil masks stop a proxy tile painting through its replacement: A/B the zoom transitions.
#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
    int TileRenderer::tileMasksMode() {
        static const int mode = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.tilemasks", property) > 0) {
                if (property[0] == '0') {
                    return 0;
                }
                if (property[0] == '1') {
                    return 1;
                }
            }
            return -1;
        }();
        return mode;
    }
    bool TileRenderer::isInline3DEnabled() {
        static const bool enabled = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return !(__system_property_get("debug.massif.inline3d", property) > 0 && property[0] == '0');
        }();
        return enabled;
    }
#else
    int TileRenderer::tileMasksMode() {
        return -1;
    }

    bool TileRenderer::isInline3DEnabled() {
        return true;
    }
#endif

#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
    int TileRenderer::terrainDemTaps() {
        static const int taps = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.demtaps", property) > 0) {
                int value = std::atoi(property);
                if (value > 0) {
                    return value;
                }
            }
            return 16;
        }();
        return taps;
    }
#else
    int TileRenderer::terrainDemTaps() {
        return 16;
    }
#endif

#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
    bool TileRenderer::isTerrainPaintOnGroundForced() {
        static const bool forced = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return __system_property_get("debug.massif.groundpaint", property) > 0 && property[0] == '1';
        }();
        return forced;
    }
#else
    bool TileRenderer::isTerrainPaintOnGroundForced() {
        return false;
    }
#endif

    bool TileRenderer::onDrawFrame(float deltaSeconds, const ViewState& viewState) {
        auto lock = lockTimed();
        VT_STAT_CLOCK(passClock);

        if (!initializeRenderer()) {
            return false;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return false;
        }

        // vt has no logger, and the map still draws after the fallback, so it would go unnoticed.
        if (!_essl3FallbackReported && tileRenderer->hasShaderVersionFallback()) {
            _essl3FallbackReported = true;
            Log::Warn("TileRenderer: a shader fell back from GLSL ES 3.00 to 1.00");
        }

        cglib::mat4x4<double> modelViewMat = viewState.getModelviewMat() * cglib::translate4_matrix(cglib::vec3<double>(_horizontalLayerOffset, 0, 0));
        vt::ViewState vtViewState(viewState.getProjectionMat(), modelViewMat, viewState.getRenderZoom(), viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        vtViewState.zoomScale *= static_cast<float>(viewState.worldPerInternal());
        vtViewState.styleZoomShift = _styleZoomShift.load();
        vtViewState.planarProjection = isPlanarProjectionMode();
        vtViewState.labelPerspectiveScaling = _labelPerspectiveScaling;
        vtViewState.lightBrightness = _resolvedBrightness;
        vtViewState.focusDistance = static_cast<float>(cglib::length(viewState.getCameraPos() - viewState.getFocusPos())); // see prepareFrameUnsafe
        vtViewState.deviceResolution = static_cast<float>(viewState.getHeight());
        if (auto options = _options.lock()) {
            vtViewState.labelViewDistance = options->getLabelViewDistance();
        }
        tileRenderer->setViewState(vtViewState);
        // Device pixels per unscaled-DPI unit, so the antialias ramp is one pixel wide (lineFsh).
        tileRenderer->setLineAntialiasScale(viewState.getNormalizedResolution() > 0 ? viewState.getHeight() / viewState.getNormalizedResolution() : 1.0f);
        tileRenderer->setInteractionMode(_interactionMode);
        tileRenderer->setRasterFilterMode(_rasterFilterMode);
        tileRenderer->setLayerBlendingSpeed(_layerBlendingSpeed);
        tileRenderer->setLabelBlendingSpeed(_labelBlendingSpeed);
        tileRenderer->setRendererLayerFilter(_rendererLayerFilter);

        // Surface rebuilds on elevation changes are debounced: the initial load changes it almost every frame.
        bool terrainMode = false;
        float terrainDepthBias = 0.0f;
        std::shared_ptr<TerrainOptions> activeTerrainOptions;
        if (auto options = _options.lock()) {
            {
                if (auto terrainOptions = options->getTerrainOptions()) {
                    if (terrainOptions->isActive()) {
                        terrainMode = true;
                        // Small equality slack only: a large clip-space bias is hundreds of metres far away.
                        terrainDepthBias = terrainOptions->getDepthBias() * 0.1f;
                        activeTerrainOptions = terrainOptions;
                        const std::shared_ptr<ElevationManager>& elevationManager = terrainOptions->getElevationManager();
                        // CPU bases come from the texture cache, which fills frames after the grid; scoped to landed tiles.
                        if (std::shared_ptr<ElevationTextureCache> elevationTextureCache = _elevationTextureCache) {
                            const std::vector<MapTile>& contentChanges = elevationTextureCache->getFrameContentChanges();
                            if (!contentChanges.empty()) {
                                std::vector<vt::TileId> contentTileIds;
                                contentTileIds.reserve(contentChanges.size());
                                for (const MapTile& mapTile : contentChanges) {
                                    contentTileIds.emplace_back(mapTile.getZoom(), mapTile.getX(), mapTile.getY());
                                }
                                tileRenderer->invalidateExtrusionBases(contentTileIds);
                                tileRenderer->invalidateLabelElevation(contentTileIds);
                            }
                        }
                        // Every CPU height carries the exaggeration: a 2D/3D ramp step invalidates the whole screen.
                        float exaggeration = elevationManager->getExaggeration();
                        if (exaggeration != _elevationExaggeration) {
                            _elevationExaggeration = exaggeration;
                            tileRenderer->invalidateLabelElevation();
                        }
                        unsigned int elevationVersion = elevationManager->getVersion();
                        if (elevationVersion != _elevationVersion) {
                            auto now = std::chrono::steady_clock::now();
                            // Scale-only changes keep the data version: the GPU displaces.
                            unsigned int elevationDataVersion = elevationManager->getDataVersion();
                            bool scaleOnly = (_elevationDataVersion != 0 && elevationDataVersion == _elevationDataVersion);
                            _elevationDataVersion = elevationDataVersion;

                            // Bases carry the exaggeration too; not narrowed, an unmoved base uploads nothing.
                            tileRenderer->invalidateExtrusionBases();

                            std::vector<MapTile> changedTiles;
                            if (scaleOnly) {
                                _elevationVersion = elevationVersion; // labels already invalidated above
                            } else if (_elevationVersion != 0 && elevationManager->getChangedTiles(_elevationVersion, changedTiles)) {
                                _elevationVersion = elevationVersion;
                                std::vector<vt::TileId> changedTileIds;
                                changedTileIds.reserve(changedTiles.size());
                                for (const MapTile& changedTile : changedTiles) {
                                    changedTileIds.emplace_back(changedTile.getZoom(), changedTile.getX(), changedTile.getY());
                                }
                                tileRenderer->invalidateTileSurfaces(changedTileIds);
                                // Targeted: a blanket re-anchor samples elevation per label vertex, hundreds of ms.
                                tileRenderer->invalidateLabelElevation(changedTileIds);
                            } else if (!_lastSurfaceResetTime || now - *_lastSurfaceResetTime > std::chrono::milliseconds(SURFACE_RESET_DELAY)) {
                                _elevationVersion = elevationVersion;
                                _lastSurfaceResetTime = now;
                                tileRenderer->resetTileSurfaces();
                                tileRenderer->invalidateLabelElevation();
                            } else if (auto mapRenderer = _mapRenderer.lock()) {
                                mapRenderer->requestRedraw(); // apply the pending rebuild on a later frame
                                // An elevation version that never settles would make this an endless render loop.
                                static int pendingRebuildFrames = 0;
                                if ((++pendingRebuildFrames % 300) == 0) {
                                    Log::Infof("TileRenderer: %d frames spent waiting on an elevation rebuild, version %u", pendingRebuildFrames, elevationVersion);
                                }
                            }
                        }
                    }
                }
            }
        }
        // GPU draping needs vertex texture fetch; without it CPU displacement with polygon offsets stays.
        vt::GLTileRenderer::TerrainTextureProvider terrainTextureProvider;
        if (terrainMode && activeTerrainOptions) {
            if (_maxVertexTextureUnits < 0) {
                GLint maxVertexTextureUnits = 0;
                glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxVertexTextureUnits);
                _maxVertexTextureUnits = maxVertexTextureUnits;
                if (maxVertexTextureUnits <= 0) {
                    Log::Warn("TileRenderer::onDrawFrame: No vertex texture support, using CPU terrain displacement");
                }
            }
            if (_maxVertexTextureUnits > 0) {
                std::shared_ptr<ElevationManager> elevationManager = activeTerrainOptions->getElevationManager();
                if (elevationManager) {
                    // Node field density for every elevation consumer; mesh resolution is only its default.
                    int nodeResolution = activeTerrainOptions->getSurfaceNodeResolution();
                    elevationManager->setSurfaceResolution(nodeResolution > 0 ? nodeResolution : activeTerrainOptions->getMeshResolution());
                }
                // One cache per map (MapRenderer): a cache per layer meant an encode thread each.
                _elevationTextureCache.reset();
                if (elevationManager) {
                    if (auto mapRenderer = _mapRenderer.lock()) {
                        _elevationTextureCache = mapRenderer->getElevationTextureCache(elevationManager);
                    }
                }
                if (_elevationTextureCache) {
                    // Per-fragment paint may exceed the mesh level cap; each level is 4x the working set.
                    _elevationTextureCache->requestDetailLevels(_terrainPaintEnabled && _terrainPaintFullDetail ? terrainPaintDetailLevels() : 0);
                    std::shared_ptr<ElevationTextureCache> elevationTextureCache = _elevationTextureCache;
                    terrainTextureProvider = [elevationTextureCache](const vt::TileId& tileId, vt::GLTileRenderer::TerrainTexture& terrainTexture) {
                        return elevationTextureCache->getTexture(tileId, terrainTexture);
                    };
                    // Per-layer depth domains in painter's order; a stride would shift vector element depth tests.
                    terrainDepthBias = 0.0f;
                }
            }
        }
        tileRenderer->setTerrainTextureProvider(terrainTextureProvider);
        if (terrainMode && activeTerrainOptions) {
            std::shared_ptr<ElevationManager> elevationManager = activeTerrainOptions->getElevationManager();
            // smooth=false reads the grid the texture entry retains. NaN, not 0, when there is no data,
            // or the label is anchored under the terrain and marked clean for good.
            std::shared_ptr<ElevationTextureCache> labelTextureCache = _elevationTextureCache;
            int labelZoom = static_cast<int>(viewState.getZoom());
            bool firstPerson = false;
            if (auto options = _options.lock()) {
                firstPerson = options->getFreeRoamMode() == FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON;
            }
            int firstPersonZoom = elevationManager->getDetailZoomLimit();
            tileRenderer->setLabelElevationProvider([elevationManager, labelTextureCache, labelZoom, firstPerson, firstPersonZoom](const cglib::vec3<double>& pos) {
                double height = 0;
                // Bounded ancestor walk, or POIs hang off a coarse ancestor. First person reads only what is held,
                // at the finest zoom: its cut ignores the camera zoom, and per-label prefetch evicted the DEM.
                if (labelTextureCache && !firstPerson) {
                    if (labelTextureCache->getDisplayHeight(pos(0), pos(1), labelZoom, false, height, ElevationTextureCache::LABEL_MAX_ANCESTOR_LEVELS)) {
                        return height;
                    }
                } else {
                    if (labelTextureCache && labelTextureCache->getDisplayHeight(pos(0), pos(1), firstPersonZoom, false, height, firstPersonZoom, false)) {
                        return height;
                    }
                    if (elevationManager->getDisplayHeightCached(pos(0), pos(1), height)) {
                        return height;
                    }
                }
                return std::numeric_limits<double>::quiet_NaN();
            });
            // Label anchors are internal units, span chords vt-normalized.
            tileRenderer->setLabelPositionScale(1.0 / Const::WORLD_SIZE);
#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
            {
                // debug.massif.labelanchor 0: anchor labels in the frame, the pre-2026-09 path.
                char property[PROP_VALUE_MAX] = { 0 };
                tileRenderer->setLabelAnchorOnCull(!(__system_property_get("debug.massif.labelanchor", property) > 0 && property[0] == '0'));
            }
#endif
            // Extrusions bake their ground, so they read the texture cache (grids get evicted while drawn).
            // Pushed only on a source change: pushing re-resolves every building base. vt passes normalized coords.
            std::shared_ptr<ElevationTextureCache> elevationTextureCache = _elevationTextureCache;
            std::pair<const void*, const void*> providerKey(tileRenderer.get(), elevationTextureCache
                ? static_cast<const void*>(elevationTextureCache.get())
                : static_cast<const void*>(elevationManager.get()));
            if (_extrusionProviderKey != providerKey) {
                _extrusionProviderKey = providerKey;
                if (elevationTextureCache) {
                    tileRenderer->setExtrusionElevationProvider([elevationTextureCache](const cglib::vec3<double>& pos, int zoom, bool smooth, double& height) {
                        return elevationTextureCache->getDisplayHeight(pos(0) * Const::WORLD_SIZE, pos(1) * Const::WORLD_SIZE, zoom, smooth, height);
                    });
                } else {
                    tileRenderer->setExtrusionElevationProvider([elevationManager](const cglib::vec3<double>& pos, int, bool, double& height) {
                        return elevationManager->getDisplayHeightCached(pos(0) * Const::WORLD_SIZE, pos(1) * Const::WORLD_SIZE, height);
                    });
                }
            }
        } else {
            tileRenderer->setLabelElevationProvider(std::function<double(const cglib::vec3<double>&)>());
            if (_extrusionProviderKey.first || _extrusionProviderKey.second) {
                _extrusionProviderKey = { nullptr, nullptr };
                tileRenderer->setExtrusionElevationProvider(std::function<bool(const cglib::vec3<double>&, int, bool, double&)>());
            }
        }
        tileRenderer->setTerrainMode(terrainMode, terrainDepthBias);
        tileRenderer->setTileMasks(tileMasksMode());
        // Chord error shrinks quadratically with mesh resolution; 32 maps to 1.
        float terrainSlackScale = 1.0f;
        if (terrainMode && activeTerrainOptions) {
            float resolutionRatio = 32.0f / std::max(32, activeTerrainOptions->getMeshResolution());
            terrainSlackScale = resolutionRatio * resolutionRatio;
        }
        tileRenderer->setTerrainSlackScale(terrainSlackScale);
        // Tangram's model: one shared grid surface, painter-order depth on top. Needs GPU draping.
        bool regularGrid = terrainMode && activeTerrainOptions && (bool) terrainTextureProvider;
        tileRenderer->setTerrainRegularGrid(regularGrid, activeTerrainOptions ? activeTerrainOptions->getMeshResolution() : 0);
        // Maplibre-style RTT draping; the drape UV is the regular grid's tile-local vertex position.
        bool drapeFills = regularGrid && activeTerrainOptions->isDrapeFillsEnabled();
        // Tangram's per-step shift between coplanar style layers, unscaled: scaling it let far content
        // over a near ridge. See docs/internals/rendering/05-depth-model.md.
        float contentDepthShift = getTerrainContentDepthShift();
        if (_terrainGroundActive && contentDepthShift == 0.0f) {
            contentDepthShift = TERRAIN_TANGRAM_DEPTH_SHIFT;
        }
        tileRenderer->setTerrainContentDepthShift(contentDepthShift);
        // Lines chord over the relief and get cut into fragments; converted at the equator scale.
        tileRenderer->setTerrainLineClearance(static_cast<float>(terrainLineClearanceMeters() * Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE));
        tileRenderer->setTerrainEdgeStitching(regularGrid && activeTerrainOptions && activeTerrainOptions->isTileEdgeStitchingEnabled());
        // Draped lines skip subdivision but blur at drape texture resolution.
        bool drapeLines = drapeFills && activeTerrainOptions && activeTerrainOptions->isDrapeLinesEnabled();
        tileRenderer->setTerrainDrapeFills(drapeFills, drapeLines);
        // Off, a span feature drapes like the ground (GLTileRenderer::setSpansEnabled).
        tileRenderer->setSpansEnabled(terrainMode && activeTerrainOptions && activeTerrainOptions->isBridges3DEnabled());
        // Layers kept sharp (contours by default) are drawn live instead of draped.
        tileRenderer->setNoDrapeLayerFilter(noDrapeLayerFilter(
            activeTerrainOptions ? activeTerrainOptions->getNoDrapeLayerFilter() : std::string()));
        tileRenderer->setTerrainDrapeResolution(resolveDrapeResolution(activeTerrainOptions ? activeTerrainOptions->getDrapeResolution() : 0, viewState, _options.lock(),
            activeTerrainOptions ? static_cast<std::size_t>(activeTerrainOptions->getDrapeCacheSize()) : 0,
            activeTerrainOptions ? activeTerrainOptions->getDrapeWorkingSet() : 0));
        VT_STAT_SPLIT(pass2DStateNs, passClock);
        vt::GLTileRenderer::TerrainLighting terrainLighting;
        if (auto options = _options.lock()) {
            // Style values win over LightOptions; re-read every frame, so both may depend on zoom.
            ResolvedLighting lighting = resolveLighting(options->getLightOptions(), _styleEnvironment);
            // Extrusions have their own pair; captured for the draw-time 3D lighting callback.
            _buildingLightIntensity = lighting.buildingLightIntensity;
            _buildingAmbient = lighting.buildingAmbient;
            _buildingVerticalGradient = lighting.buildingVerticalGradient;
            _buildingRoofShade = lighting.buildingRoofShade;
            _buildingLightingMapLibre = lighting.buildingLightingMapLibre;
            _groundAOIntensity = lighting.buildingAoIntensity;
            _groundAOAttenuation = lighting.buildingAoGroundAttenuation;
            _buildingHeightScale = lighting.buildingHeightScale;
        _buildingHeightViewScale = lighting.buildingHeightViewScale;
            _buildingGrowOnAppear = lighting.buildingGrowOnAppear;
            _buildingFadeOnAppear = lighting.buildingFadeOnAppear;
            _resolvedSunDir = lighting.sunDir;
            _resolvedBuildingSunDir = lighting.sunDir;
            _resolvedSunColor = lighting.sunColor;
            _resolvedAmbientColor = lighting.ambientColor;
            std::shared_ptr<LightOptions> lightOptions = options->getLightOptions();
            float moon = (lightOptions && lightOptions->isDayCycleLightsEnabled()) ? DayCycleLight::moonWeight(lighting.sunDir(2)) : 0.0f;
            if (moon > 0.0f) {
                cglib::vec3<float> moonDir(DayCycleLight::MOON_DIR[0], DayCycleLight::MOON_DIR[1], DayCycleLight::MOON_DIR[2]);
                _resolvedBuildingSunDir = cglib::unit(lighting.sunDir * (1.0f - moon) + moonDir * moon);
                auto toward = [moon](unsigned char from, float to) {
                    return static_cast<unsigned char>(std::lround(from + (to * 255.0f - from) * moon));
                };
                const Color& ambient = lighting.ambientColor;
                _resolvedAmbientColor = Color(toward(ambient.getR(), DayCycleLight::NIGHT_BUILDING_AMBIENT[0]),
                                              toward(ambient.getG(), DayCycleLight::NIGHT_BUILDING_AMBIENT[1]),
                                              toward(ambient.getB(), DayCycleLight::NIGHT_BUILDING_AMBIENT[2]), ambient.getA());
            }
            _buildingEmissive = lighting.buildingEmissive;
            _backgroundEmissive = lighting.backgroundEmissive;
            _resolvedRadiance = lighting.radiance;
            _resolvedBrightness = lighting.brightness;
            // The shared ground pass is lit too, or it and its shadow stay unlit.
            if ((drapeFills || _terrainGroundActive) && lighting.terrainLightingEnabled) {
                terrainLighting = buildTerrainLighting(lighting);
            }

            // Camera-relative range in internal units: fogs a plain 2D map too.
            ResolvedFog fog = resolveFog(options->getFogOptions(), _styleEnvironment, lighting, viewState.calculateCameraDistance(), viewState.getTilt());
            tileRenderer->setFog(vt::Color(fog.color.getR() / 255.0f, fog.color.getG() / 255.0f, fog.color.getB() / 255.0f, fog.color.getA() / 255.0f),
                                 fog.startDistance, fog.distance, fog.rangeScale, fog.horizonBlend);
            tileRenderer->setFogColors(vt::Color(fog.highColor.getR() / 255.0f, fog.highColor.getG() / 255.0f, fog.highColor.getB() / 255.0f, fog.highColor.getA() / 255.0f),
                                       vt::Color(fog.spaceColor.getR() / 255.0f, fog.spaceColor.getG() / 255.0f, fog.spaceColor.getB() / 255.0f, fog.spaceColor.getA() / 255.0f));
            float metersPerUnit = static_cast<float>(Const::EARTH_CIRCUMFERENCE / Const::WORLD_SIZE);
            tileRenderer->setFogVertical(fog.verticalRangeStart, fog.verticalRangeEnd, metersPerUnit,
                                         static_cast<float>(viewState.getCameraPos()(2)) * metersPerUnit);
            tileRenderer->setFogRayBasis(FogShader::rayBasis(viewState));
            if (std::shared_ptr<FogOptions> fogOptions = options->getFogOptions()) {
                tileRenderer->setFogShaderSource(fogOptions->getShaderSource());
            }
        }
        tileRenderer->setTerrainLighting(terrainLighting);
        tileRenderer->setGroundAO(_groundAOIntensity, _groundAOAttenuation);
        tileRenderer->setBuildingHeight(_buildingHeightScale, _buildingHeightViewScale, _buildingGrowOnAppear, _buildingFadeOnAppear);
        tileRenderer->setTerrainDepthWrite(terrainMode && _terrainDepthWriteMode);
        if (auto options = _options.lock()) {
            tileRenderer->setDebugTileBorders(options->isDebugTileBorders());
        }
        tileRenderer->setDebugWireframe(false); // debug: terrain mesh wireframe + stencil overlay
        tileRenderer->setDebugSurfacePrefill(false); // debug: facing-coded terrain pre-fill (magenta front / cyan back)
        // MapRenderer draws the base fill before all layers; the per-layer pre-pass stays depth-only.
        tileRenderer->setTerrainBackgroundColor(vt::Color());
        updateLabelOcclusionTest(tileRenderer, viewState, activeTerrainOptions);


        _mapRotation = viewState.getRotation();
        _viewDir = cglib::unit(viewState.getFocusPosNormal());
        if (auto options = _options.lock()) {
            MapPos internalFocusPos = viewState.getProjectionSurface()->calculateMapPos(viewState.getFocusPos());
            MapVec normalIlluminationDir = options->getMainLightDirection();
            if (_normalIlluminationDirection != MapVec(0,0,0)) {
                normalIlluminationDir = _normalIlluminationDirection;
            }
            if (_normalIlluminationMapRotationEnabled) {
                double y = normalIlluminationDir.getY();
                double x = normalIlluminationDir.getX();
                // Counter-rotate the compass azimuth so the light stays anchored to the viewport, keeping the
                // horizontal length (an acos(y) form assumes a unit xy).
                double xyLength = std::sqrt(x * x + y * y);
                double azimuthal = std::atan2(x, y) * Const::RAD_TO_DEG - _mapRotation;
                double sin = std::sin(azimuthal * Const::DEG_TO_RAD) * xyLength;
                double cos = std::cos(azimuthal * Const::DEG_TO_RAD) * xyLength;
                normalIlluminationDir = MapVec(sin, cos, normalIlluminationDir.getZ());
            }

            _normalLightDir = cglib::vec3<float>::convert(cglib::unit(viewState.getProjectionSurface()->calculateVector(internalFocusPos, normalIlluminationDir)));
        }

        bool refresh = false;
        try {
            VT_STAT_SPLIT(pass2DLightNs, passClock);
            refresh = prepareFrameUnsafe(deltaSeconds, viewState);
            VT_STAT_SPLIT(pass2DPrepareNs, passClock);

            tileRenderer->renderGeometry(true, false);
            VT_STAT_SPLIT(pass2DGeometryNs, passClock);
            if (_labelOrder == 0) {
                tileRenderer->renderLabels(true, false);
            }
            VT_STAT_SPLIT(pass2DLabels2DNs, passClock);
            if (_buildingOrder == 0) {
                tileRenderer->renderGeometry(false, true);
            }
            VT_STAT_SPLIT(pass2DExtrusionNs, passClock);
            if (_labelOrder >= 0 && drawsBillboardLabelsHere(0)) {
                tileRenderer->renderLabels(false, true);
            }
            VT_STAT_SPLIT(pass2DLabels3DNs, passClock);
        }
        catch (const std::exception& ex) {
            Log::Errorf("TileRenderer::onDrawFrame: Rendering failed: %s", ex.what());
        }
    
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        GLContext::CheckGLError("TileRenderer::onDrawFrame");
        return refresh;
    }
    
    bool TileRenderer::onDrawFrame3D(float deltaSeconds, const ViewState& viewState) {
        auto lock = lockTimed();

        // Cleared up front: leaking the latch past an early return would skip every later startFrame.
        _framePrepared = false;

        if (!_vtRenderer) {
            return false;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return false;
        }

        bool refresh = false;
        try {
            VT_STAT_CLOCK(passClock);
            if (_labelOrder == 1) {
                tileRenderer->renderLabels(true, false);
            }
            VT_STAT_SPLIT(pass3DLabels2DNs, passClock);
            if (_buildingOrder == 1) {
                // Inline (tangram's way): nothing after the extrusions depth-tests against them.
                tileRenderer->renderGeometry(false, true, isInline3DEnabled());
            }
            VT_STAT_SPLIT(pass3DGeometryNs, passClock);
            if (_labelOrder >= 0 && drawsBillboardLabelsHere(1)) {
                tileRenderer->renderLabels(false, true);
            }
            VT_STAT_SPLIT(pass3DLabels3DNs, passClock);

            refresh = tileRenderer->endFrame();
        }
        catch (const std::exception& ex) {
            Log::Errorf("TileRenderer::onDrawFrame3D: Rendering failed: %s", ex.what());
        }

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        GLContext::CheckGLError("TileRenderer::onDrawFrame3D");
        return refresh;
    }
    
    bool TileRenderer::consumeLabelPlacementOwed() {
        std::lock_guard<std::mutex> lock(_mutex);
        bool owed = _labelPlacementOwed;
        _labelPlacementOwed = false;
        return owed;
    }

    void TileRenderer::snapLabelTransition() {
        std::shared_ptr<vt::GLTileRenderer> tileRenderer;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_vtRenderer) {
                tileRenderer = _vtRenderer->getTileRenderer();
            }
        }
        if (tileRenderer) {
            tileRenderer->snapLabelTransition();
        }
    }

    void TileRenderer::restartLabelPlacement() {
        std::shared_ptr<vt::GLTileRenderer> tileRenderer;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_vtRenderer) {
                tileRenderer = _vtRenderer->getTileRenderer();
            }
        }
        if (tileRenderer) {
            tileRenderer->restartLabelPlacement();
        }
    }

    bool TileRenderer::cullLabels(vt::LabelCuller& culler, const ViewState& viewState, bool& finished) {
        std::shared_ptr<vt::GLTileRenderer> tileRenderer;
        cglib::mat4x4<double> modelViewMat;
        {
            std::lock_guard<std::mutex> lock(_mutex);

            if (_vtRenderer) {
                tileRenderer = _vtRenderer->getTileRenderer();
            }
            modelViewMat = viewState.getModelviewMat() * cglib::translate4_matrix(cglib::vec3<double>(_horizontalLayerOffset, 0, 0));
        }

        if (!tileRenderer) {
            return false;
        }
        vt::ViewState cullViewState(viewState.getProjectionMat(), modelViewMat, viewState.getRenderZoom(),
viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        cullViewState.zoomScale *= static_cast<float>(viewState.worldPerInternal());
        cullViewState.styleZoomShift = _styleZoomShift.load();
        cullViewState.planarProjection = isPlanarProjectionMode(); // keep culling envelopes consistent with the rendered label sizes
        // A hidden label must take no collision slot from a visible one.
        culler.setOcclusionTest(getLabelOcclusionTest());
        cullViewState.labelPerspectiveScaling = _labelPerspectiveScaling;
        cullViewState.lightBrightness = _resolvedBrightness;
        cullViewState.focusDistance = static_cast<float>(cglib::length(viewState.getCameraPos() - viewState.getFocusPos()));
        // Must match the draw pass, or callouts are measured at the wrong size.
        cullViewState.deviceResolution = static_cast<float>(viewState.getHeight());
        // The placement search applies this cut before the culler does.
        if (auto options = _options.lock()) {
            cullViewState.labelViewDistance = options->getLabelViewDistance();
            // Must match the tile culler's band (ViewState::getLabelFrustum).
            float labelPadding = options->getLabelPadding();
            if (labelPadding >= 0.0f) {
                cullViewState.setLabelPadding(labelPadding);
            }
        }
        culler.setViewState(cullViewState);

        try {
            finished = tileRenderer->cullLabels(culler) && finished;
        }
        catch (const std::exception& ex) {
            Log::Errorf("TileRenderer::cullLabels: Culling failed: %s", ex.what());
            return false; // and 'finished' is left alone - retrying a layer that threw will not help
        }
        return true;
    }
    
    bool TileRenderer::refreshTiles(const std::vector<std::shared_ptr<TileDrawData> >& drawDatas, const std::vector<std::shared_ptr<const vt::Tile> >& spanReferenceTiles) {
        // Lock wait timed apart from the work: tile threads hold this mutex while storing decoded tiles.
        VT_STAT_CLOCK(refreshClock);
        std::shared_ptr<vt::GLTileRenderer> tileRenderer;
        std::map<vt::TileId, std::shared_ptr<const vt::Tile> > tiles, labelOnlyTiles, shadowCasterTiles;
        int teleportOffset = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            VT_STAT_SPLIT(refreshTilesLockNs, refreshClock);

            // Preloading tiles lie outside the frustum: labels placed before they scroll in, geometry not drawn.
            for (const std::shared_ptr<TileDrawData>& drawData : drawDatas) {
                auto& target = (drawData->isShadowCasterTile() ? shadowCasterTiles : (drawData->isPreloadingTile() ? labelOnlyTiles : tiles));
                target[drawData->getVTTileId()] = drawData->getVTTile();
            }

            bool changed = (tiles != _tiles) || (labelOnlyTiles != _labelOnlyTiles) || (shadowCasterTiles != _shadowCasterTiles) ||
                           (spanReferenceTiles != _spanReferenceTiles) || (_horizontalLayerOffset != 0);
            if (!changed) {
                return false;
            }

            if (_vtRenderer) {
                tileRenderer = _vtRenderer->getTileRenderer();
            }
            teleportOffset = (int)std::round(_horizontalLayerOffset / Const::WORLD_SIZE);
            _tiles = tiles;
            _labelOnlyTiles = labelOnlyTiles;
            _shadowCasterTiles = shadowCasterTiles;
            _spanReferenceTiles = spanReferenceTiles;
            _horizontalLayerOffset = 0;
        }

        // Off the mutex: setVisibleTiles rebuilds label maps and would block the render thread.
        if (tileRenderer) {
            if (teleportOffset != 0) {
                tileRenderer->teleportVisibleTiles(teleportOffset, 0);
            }
            tileRenderer->setVisibleTiles(tiles, labelOnlyTiles, spanReferenceTiles, shadowCasterTiles);
        }
        // Changed path only, including setVisibleTiles.
        VT_STAT_SPLIT(refreshTilesNs, refreshClock);
        return true;
    }

    void TileRenderer::calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, float radius, std::vector<vt::GLTileRenderer::GeometryIntersectionInfo>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_vtRenderer) {
            return;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return;
        }

        tileRenderer->setClickHandlerLayerFilter(_clickHandlerLayerFilter);

        // Terrain heights are applied on the GPU: pick vertically below the ray's terrain hit.
        cglib::ray3<double> geometryRay = ray;
        if (auto options = _options.lock()) {
            if (options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
                if (auto terrainOptions = options->getTerrainOptions()) {
                    if (terrainOptions->isActive()) {
                        double t = 0;
                        if (terrainOptions->getElevationManager()->intersectRay(ray, t)) {
                            cglib::vec3<double> hitPos = ray(t);
                            geometryRay = cglib::ray3<double>(cglib::vec3<double>(hitPos(0), hitPos(1), Const::MAX_HEIGHT), cglib::vec3<double>(0, 0, -1));
                        }
                    }
                }
            }
        }

        std::vector<cglib::ray3<double> > geometryRays = { geometryRay };
        std::vector<cglib::ray3<double> > labelRays = { ray }; // labels are anchored at terrain height, use the original ray
        tileRenderer->findGeometryIntersections(geometryRays, radius, radius, true, false, results);
        if (_labelOrder == 0) {
            tileRenderer->findLabelIntersections(labelRays, radius, true, false, results);
        }
        if (_buildingOrder == 0) {
            tileRenderer->findGeometryIntersections(geometryRays, radius, radius, false, true, results);
        }
        if (_labelOrder == 0) {
            tileRenderer->findLabelIntersections(labelRays, radius, false, true, results);
        }
    }
        
    void TileRenderer::calculateRayIntersectedElements3D(const cglib::ray3<double>& ray, const ViewState& viewState, float radius, std::vector<vt::GLTileRenderer::GeometryIntersectionInfo>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_vtRenderer) {
            return;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return;
        }

        std::vector<cglib::ray3<double> > rays = { ray };
        if (_labelOrder == 1) {
            tileRenderer->findLabelIntersections(rays, radius, true, false, results);
        }
        if (_buildingOrder == 1) {
            tileRenderer->findGeometryIntersections(rays, radius, radius, false, true, results);
        }
        if (_labelOrder == 1) {
            tileRenderer->findLabelIntersections(rays, radius, false, true, results);
        }
    }

    void TileRenderer::calculateRayIntersectedBitmaps(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<vt::GLTileRenderer::BitmapIntersectionInfo>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_vtRenderer) {
            return;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return;
        }

        std::vector<cglib::ray3<double> > rays = { ray };
        tileRenderer->findBitmapIntersections(rays, results);
    }

    Color TileRenderer::evaluateColorFunc(const vt::ColorFunction& colorFunc, const ViewState& viewState, float brightness, float zoomShift) {
        cglib::mat4x4<double> modelViewMat = viewState.getModelviewMat();
        vt::ViewState vtViewState(viewState.getProjectionMat(), modelViewMat, viewState.getRenderZoom(),
viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        vtViewState.zoomScale *= static_cast<float>(viewState.worldPerInternal());
        vtViewState.lightBrightness = brightness;
        vtViewState.styleZoomShift = zoomShift;
        return Color(colorFunc(vtViewState).value());
    }

    void TileRenderer::setStyleEnvironment(const StyleEnvironment& env) {
        std::lock_guard<std::mutex> lock(_mutex);

        _styleEnvironment = env;
        _styleZoomShift.store(env.zoomShift);
    }

    float TileRenderer::evaluateFloatFunc(const vt::FloatFunction& floatFunc, const ViewState& viewState, float brightness, float zoomShift) {
        cglib::mat4x4<double> modelViewMat = viewState.getModelviewMat();
        vt::ViewState vtViewState(viewState.getProjectionMat(), modelViewMat, viewState.getRenderZoom(), viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        vtViewState.zoomScale *= static_cast<float>(viewState.worldPerInternal());
        vtViewState.lightBrightness = brightness;
        vtViewState.styleZoomShift = zoomShift;
        return floatFunc(vtViewState);
    }

    float TileRenderer::getTerrainContentDepthShift() {
#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
        static const float depthShift = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.depthshift", property) > 0) {
                return static_cast<float>(std::atof(property));
            }
            return 0.0f;
        }();
        return depthShift;
#else
        return 0.0f;
#endif
    }

    std::optional<std::regex> TileRenderer::noDrapeLayerFilter(const std::string& optionFilter) {
        std::string pattern = optionFilter;
#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
        char property[PROP_VALUE_MAX] = { 0 };
        if (__system_property_get("debug.massif.nodrapelayers", property) > 0 && property[0]) {
            pattern = (std::strcmp(property, "none") == 0 ? std::string() : property);
        }
#endif
        // Compiling a regex per frame is not free, and this changes about never.
        static std::string cachedPattern;
        static std::optional<std::regex> cachedFilter;
        static bool cacheValid = false;
        if (cacheValid && cachedPattern == pattern) {
            return cachedFilter;
        }
        cachedPattern = pattern;
        cachedFilter.reset();
        if (!pattern.empty()) {
            try {
                cachedFilter = std::regex(pattern);
            } catch (const std::exception& ex) {
                Log::Errorf("TileRenderer::noDrapeLayerFilter: bad pattern '%s': %s", pattern.c_str(), ex.what());
            }
        }
        cacheValid = true;
        return cachedFilter;
    }

#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
    float TileRenderer::terrainLineClearanceMeters() {
        static const float meters = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.lineclearance", property) > 0) {
                return static_cast<float>(std::atof(property));
            }
            return DEFAULT_LINE_CLEARANCE_METERS;
        }();
        return meters;
    }
#else
    float TileRenderer::terrainLineClearanceMeters() {
        return DEFAULT_LINE_CLEARANCE_METERS;
    }
#endif

    int TileRenderer::terrainPaintDetailLevels() {
#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
        static const int levels = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.paintdetail", property) > 0) {
                int value = std::atoi(property);
                if (value >= 0 && value <= 4) {
                    return value;
                }
            }
            return DEFAULT_PAINT_DETAIL_LEVELS;
        }();
        return levels;
#else
        return DEFAULT_PAINT_DETAIL_LEVELS;
#endif
    }

    bool TileRenderer::isPlanarProjectionMode() const {
        // Label size correction belongs to the projection: a tilted flat map divides by w too.
        if (auto options = _options.lock()) {
            return options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR;
        }
        return false;
    }

    void TileRenderer::updateLabelOcclusionTest(const std::shared_ptr<vt::GLTileRenderer>& tileRenderer, const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions) {
        if (!terrainOptions || !terrainOptions->isBillboardOcclusionEnabled()) {
            _labelOcclusionState.reset();
            tileRenderer->setLabelOcclusionTest(std::function<bool(const cglib::vec3<double>&)>());
            setLabelOcclusionTestCopy(std::function<bool(const cglib::vec3<double>&)>());
            return;
        }

        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
        if (!_labelOcclusionState) {
            _labelOcclusionState = std::make_shared<LabelOcclusionState>();
        }
        std::shared_ptr<LabelOcclusionState> state = _labelOcclusionState;

        cglib::vec3<double> cameraPos = viewState.getCameraPos();
        double moveThreshold = 0.01 * cglib::length(viewState.getFocusPos() - cameraPos);
        unsigned int elevationVersion = elevationManager->getVersion();
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (cglib::length(cameraPos - state->cameraPos) > moveThreshold || elevationVersion != state->elevationVersion) {
                state->results.clear();
                state->cameraPos = cameraPos;
                state->elevationVersion = elevationVersion;
            }
        }

        // Terrain in front of the anchor nearer than 1 / (1 + tolerance) of its distance.
        double rayHitLimit = 1.0 / (1.0 + std::max(static_cast<double>(MIN_OCCLUSION_TOLERANCE), static_cast<double>(terrainOptions->getBillboardOcclusionTolerance())));
        auto rayTest = [state, elevationManager, cameraPos, rayHitLimit](const cglib::vec3<double>& pos) -> bool {
            // Cache key: position quantized to 0.1 internal units (~4 m).
            const double QUANT = 10.0;
            long long key = (static_cast<long long>(pos(0) * QUANT) * 73856093LL) ^ (static_cast<long long>(pos(1) * QUANT) * 19349663LL) ^ (static_cast<long long>(pos(2) * QUANT) * 83492791LL);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                auto it = state->results.find(key);
                if (it != state->results.end()) {
                    return it->second;
                }
            }

            // Not intersectRay, which ignores a rising ray: every valley-to-summit sight line rises.
            bool occluded = elevationManager->isSegmentBlocked(cameraPos, pos, rayHitLimit);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->results[key] = occluded;
            }
            return occluded;
        };

        // Preferred: the read-back depth buffer matches the screen and is far cheaper than ray-marching.
        if (auto mapRenderer = _mapRenderer.lock()) {
            if (TerrainRenderer* terrainRenderer = mapRenderer->getTerrainRenderer()) {
                // A new depth changes the verdicts, and a still camera asks for no placement pass.
                unsigned int depthVersion = terrainRenderer->getDepthSnapshotVersion();
                if (depthVersion != _labelOcclusionDepthVersion) {
                    _labelOcclusionDepthVersion = depthVersion;
                    _labelPlacementOwed = true;
                }
                std::weak_ptr<MapRenderer> mapRendererWeak = _mapRenderer;
                // Relative tolerance: the default absorbs anchor-vs-terrain mismatch; more labels partly hidden features.
                float occlusionTolerance = 1.0f + std::max(MIN_OCCLUSION_TOLERANCE, terrainOptions->getBillboardOcclusionTolerance());
                // Outside the read-back viewport, the elevation ray answers.
                auto depthTest = [mapRendererWeak, occlusionTolerance, rayTest](const cglib::vec3<double>& pos) {
                    auto mapRenderer = mapRendererWeak.lock();
                    if (!mapRenderer) {
                        return false;
                    }
                    TerrainRenderer* terrainRenderer = mapRenderer->getTerrainRenderer();
                    if (!terrainRenderer) {
                        return false;
                    }
                    bool answered = false;
                    bool occluded = terrainRenderer->isOccludedByTerrain(pos, occlusionTolerance, &answered);
                    return answered ? occluded : rayTest(pos);
                };
                tileRenderer->setLabelOcclusionTest(depthTest);
                setLabelOcclusionTestCopy(depthTest);
                return;
            }
        }

        tileRenderer->setLabelOcclusionTest(rayTest);
        setLabelOcclusionTestCopy(rayTest);
    }

    void TileRenderer::setLabelOcclusionTestCopy(std::function<bool(const cglib::vec3<double>&)> test) {
        std::lock_guard<std::mutex> lock(_labelOcclusionTestMutex);
        _labelOcclusionTestCopy = std::move(test);
    }

    std::function<bool(const cglib::vec3<double>&)> TileRenderer::getLabelOcclusionTest() const {
        std::lock_guard<std::mutex> lock(_labelOcclusionTestMutex);
        return _labelOcclusionTestCopy;
    }

    vt::GLTileRenderer::LightingShader TileRenderer::createNormalMapLightingShader() {
        return vt::GLTileRenderer::LightingShader(false, _normalMapLightingShader, [this](GLuint shaderProgram, const vt::ViewState& viewState) {
            // Straight colors; the shader premultiplies them, as MapLibre's does.
            glUniform4f(glGetUniformLocation(shaderProgram, "u_shadowColor"), _normalMapShadowColor.getR() / 255.0f, _normalMapShadowColor.getG() / 255.0f, _normalMapShadowColor.getB() / 255.0f, _normalMapShadowColor.getA() / 255.0f);
            glUniform4f(glGetUniformLocation(shaderProgram, "u_accentColor"), _normalMapAccentColor.getR() / 255.0f, _normalMapAccentColor.getG() / 255.0f, _normalMapAccentColor.getB() / 255.0f, _normalMapAccentColor.getA() / 255.0f);
            glUniform4f(glGetUniformLocation(shaderProgram, "u_highlightColor"), _normalMapHighlightColor.getR() / 255.0f, _normalMapHighlightColor.getG() / 255.0f, _normalMapHighlightColor.getB() / 255.0f, _normalMapHighlightColor.getA() / 255.0f);
            glUniform3fv(glGetUniformLocation(shaderProgram, "u_lightDir"), 1, _normalLightDir.data() );
            glUniform1i(glGetUniformLocation(shaderProgram, "u_method"), (_hillshadeMethod));
            glUniform1f(glGetUniformLocation(shaderProgram, "u_exaggeration"), _hillshadeExaggeration);
            // MapLibre's 'hillshade-exaggeration', from the layer's contrast; u_exaggeration scales the slope.
            glUniform1f(glGetUniformLocation(shaderProgram, "u_intensity"), _hillshadeIntensity);
            // No effect unless the normal map is elevation-encoded (HillshadeRasterTileLayer).
            glUniform1f(glGetUniformLocation(shaderProgram, "u_elevationEncoded"), _normalMapElevationEncoded ? 1.0f : 0.0f);
            glUniform2f(glGetUniformLocation(shaderProgram, "u_elevationDecode"), vt::NormalMapBuilder::ELEVATION_SCALE, vt::NormalMapBuilder::ELEVATION_OFFSET);
            glUniform1f(glGetUniformLocation(shaderProgram, "u_contrast"), _hillshadeIntensity);
            glUniform4f(glGetUniformLocation(shaderProgram, "u_contourColor"), _normalMapContourColor.getR() / 255.0f, _normalMapContourColor.getG() / 255.0f, _normalMapContourColor.getB() / 255.0f, _normalMapContourColor.getA() / 255.0f);
            glUniform1f(glGetUniformLocation(shaderProgram, "u_contourInterval"), _normalMapContourInterval);
            glUniform1f(glGetUniformLocation(shaderProgram, "u_contourWidth"), _normalMapContourWidth);
            // For per-zoom custom normal-map shaders (getMapZoom()).
            glUniform1f(glGetUniformLocation(shaderProgram, "u_zoom"), viewState.zoom);
        });
    }

    bool TileRenderer::initializeRenderer() {
        if (_vtRenderer && _vtRenderer->isValid()) {
            return true;
        }

        std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock();
        if (!mapRenderer) {
            return false;
        }

        // Null once the surface is gone - a frame still in flight has nothing to create into (#178).
        std::shared_ptr<GLResourceManager> glResourceManager = mapRenderer->getGLResourceManager();
        if (!glResourceManager) {
            return false;
        }

        Log::Debug("TileRenderer: Initializing renderer");
        _vtRenderer = glResourceManager->create<VTRenderer>(_tileTransformer);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer()) {
            tileRenderer->setVisibleTiles(_tiles, _labelOnlyTiles, {}, _shadowCasterTiles);
            // These tiles' placement pass found no GL renderer; see consumeLabelPlacementOwed.
            _labelPlacementOwed = !_tiles.empty();

            if (!std::dynamic_pointer_cast<PlanarProjectionSurface>(mapRenderer->getProjectionSurface())) {
                vt::GLTileRenderer::LightingShader lightingShader2D(true, LIGHTING_SHADER_2D, [this](GLuint shaderProgram, const vt::ViewState& viewState) {
                    glUniform3fv(glGetUniformLocation(shaderProgram, "u_viewDir"), 1, _viewDir.data());
                });
                tileRenderer->setLightingShader2D(lightingShader2D);
            }

            // Per fragment, where the shadow term exists; same sun as the terrain surface.
            vt::GLTileRenderer::LightingShader lightingShader3D(false, LIGHTING_SHADER_3D, [this](GLuint shaderProgram, const vt::ViewState& viewState) {
                // Linear, intensity folded in: the shader sums them and returns to sRGB once.
                cglib::vec3<float> sunColor = linearColor(_resolvedSunColor, _buildingLightIntensity);
                cglib::vec3<float> ambientColor = linearColor(_resolvedAmbientColor, _buildingAmbient);
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_sunDir"), 1, _resolvedBuildingSunDir.data());
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_sunColor"), 1, sunColor.data());
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_ambientColor"), 1, ambientColor.data());
                glUniform2f(glGetUniformLocation(shaderProgram, "u_verticalGradient"), _buildingVerticalGradient, _buildingRoofShade);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_emissive"), _buildingEmissive);
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_radiance"), 1, _resolvedRadiance.data());
                glUniform1f(glGetUniformLocation(shaderProgram, "u_mlMode"), _buildingLightingMapLibre ? 1.0f : 0.0f);
                // MapLibre's viewport-anchored light (ML_LIGHT_POS, y not negated), turned by the bearing.
                double bearing = viewState.rotation * Const::DEG_TO_RAD;
                float c = static_cast<float>(std::cos(bearing)), s = static_cast<float>(std::sin(bearing));
                cglib::vec3<float> light(ML_LIGHT_POS(0) * c - ML_LIGHT_POS(1) * s,
                                         ML_LIGHT_POS(0) * s + ML_LIGHT_POS(1) * c,
                                         ML_LIGHT_POS(2));
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_mlLightPos"), 1, light.data());
                glUniform2f(glGetUniformLocation(shaderProgram, "u_mlLight"), ML_LIGHT_INTENSITY, ML_VERTICAL_GRADIENT);
            });
            tileRenderer->setLightingShader3D(lightingShader3D);

            tileRenderer->setLightingShaderNormalMap(createNormalMapLightingShader());
        }

        return _vtRenderer && _vtRenderer->isValid();
    }

    // sphericalToCartesian([1.15, 210, 30]) as maplibre computes it: y not negated (the normals agree),
    // and unnormalised, as the 1.15 radius is part of the look.
    const cglib::vec3<float> TileRenderer::ML_LIGHT_POS = cglib::vec3<float>(0.2875f, -0.4980f, 0.9959f);

    const std::string TileRenderer::LIGHTING_SHADER_2D = R"GLSL(
        uniform vec3 u_viewDir;
        vec4 applyLighting(lowp vec4 color, mediump vec3 normal) {
            mediump float lighting = max(0.0, dot(normal, u_viewDir)) * 0.5 + 0.5;
            return vec4(color.rgb * lighting, color.a);
        }
    )GLSL";

    const std::string TileRenderer::LIGHTING_SHADER_3D = R"GLSL(
        uniform vec3 u_sunDir;
        uniform vec3 u_sunColor;     // linear, already scaled by the sun intensity
        uniform vec3 u_ambientColor; // linear, already scaled by the ambient intensity
        uniform vec2 u_verticalGradient; // x = how dark the foot of a wall goes, y = roof shade
        // Emitted fraction (mapbox's *-emissive-strength): 1 = as authored, 0 = fully lit.
        uniform float u_emissive;
        // Linear light on a flat upward surface (calculateGroundRadiance), for replaceable grades.
        uniform vec3 u_radiance;
        // MapLibre's fill-extrusion model for a style that lights nothing; u_mlLightPos is pre-rotated.
        uniform float u_mlMode;
        uniform vec3 u_mlLightPos;
        uniform vec2 u_mlLight; // x = intensity, y = vertical gradient
        vec4 applyLighting3D(lowp vec4 color, mediump vec3 normal, mediump float wallT, mediump float sideVertex, mediump float shadow, mediump float skyShadow) {
            if (u_mlMode > 0.5) {
                // Port of maplibre's fill_extrusion.vertex.glsl.
                mediump vec3 mlColor = color.rgb + 0.03 * color.a;
                mediump float mlValue = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
                mediump float mlDir = clamp(dot(normal, u_mlLightPos), 0.0, 1.0);
                mlDir = mix(1.0 - u_mlLight.x, max(1.0 - mlValue + u_mlLight.x, 1.0), mlDir);
                // Their gradient floor only, without the ramp above ~106 m: tall towers light slightly flatter.
                mlDir *= mix(1.0, (1.0 - u_mlLight.y) + u_mlLight.y * mix(0.7, 0.98, 1.0 - u_mlLight.x), sideVertex);
                // Our shadow still multiplies it.
                return vec4(min(mlColor * mlDir * shadow, vec3(color.a)), color.a);
            }
            // Wall-foot AO grounds the extrusion; wallT is baked from absolute height, one ramp per building.
            lowp vec3 baseColor = color.rgb * mix(u_verticalGradient.y, mix(1.0 - u_verticalGradient.x, 1.0, wallT), sideVertex);
            // Mapbox's fill-extrusion model (docs/internals/rendering/08-lighting-sky-fog.md):
            // ambient and sun sum, and the direction-aware ambient separates wall tones.
            mediump float ndl = dot(normal, u_sunDir);
            // Clamped as fill_extrusion (not the wrapped model-layer NdotL); faded below the horizon, where a
            // wall's N.L stays positive.
            mediump float sunNdl = max(0.0, ndl) * smoothstep(-0.035, 0.0, u_sunDir.z);
            // Faces turned from the sun lose up to 30% of the ambient, scaled by sun brightness.
            mediump float dirLuminance = dot(u_sunColor, vec3(0.2126, 0.7152, 0.0722));
            mediump float ambientDirectional = mix(1.0 - 0.3 * min(dirLuminance, 1.0), 1.0, min(ndl + 1.0, 1.0));
            // Environmental light blocked from below: a downward face keeps 92%, a roof all of it.
            mediump float vertical = mix(0.92, 1.0, normal.z * 0.5 + 0.5);
            // Sky is shadowed by the map only. Linearised: this sum is linear, the ground's shadow is sRGB.
            mediump float linearSky = pow(skyShadow, 2.2);
            mediump float linearSun = pow(shadow, 2.2);
            mediump vec3 lit = u_ambientColor * (vertical * ambientDirectional * linearSky) + u_sunColor * (sunNdl * linearSun);
            // Summed linear, then to sRGB: soft facades. = linearTosRGB(sRGBToLinear(color) * lit), one pow.
            lit = pow(lit, vec3(1.0 / 2.2));
            // mapbox's mix(apply_lighting(color), color, emissive_strength).
            lit = mix(lit, vec3(1.0), clamp(u_emissive, 0.0, 1.0));
            // Premultiplied, so scaling rgb alone is a valid tint and the clamp keeps rgb <= a.
            return vec4(min(baseColor * lit, vec3(color.a)), color.a);
        }
    )GLSL";

    const std::string TileRenderer::LIGHTING_SHADER_NORMALMAP = R"GLSL(
        uniform vec4 u_shadowColor;
        uniform vec4 u_highlightColor;
        uniform vec4 u_accentColor;
        uniform vec3 u_lightDir;
        uniform int u_method;
        // Vertical relief multiplier applied to the slope (HillshadeRasterTileLayer exaggeration).
        uniform float u_exaggeration;
        // MapLibre's 'hillshade-exaggeration' (HillshadeRasterTileLayer contrast), default 0.5.
        uniform float u_intensity;

        #define PI 3.141592653589793
        #define STANDARD 0
        #define COMBINED 1
        #define IGOR 2
        #define MULTIDIRECTIONAL 3
        #define BASIC 4

        // Tiles blend premultiplied (GL_ONE, GL_ONE_MINUS_SRC_ALPHA), so uniform colors are premultiplied first.
        vec4 premul(vec4 color) {
            return vec4(color.rgb * color.a, color.a);
        }

        float get_aspect(vec2 deriv) {
            return deriv.x != 0.0 ? atan(deriv.y, -deriv.x) : PI / 2.0 * (deriv.y > 0.0 ? 1.0 : -1.0);
        }

        // The GDAL-derived algorithms below scale the slope by the intensity, as MapLibre does
        // (deriv * u_exaggeration * 2.0 in its hillshade.fragment.glsl). standard_hillshade does
        // not - it feeds the intensity into its slope response curve instead.
        vec2 scale_deriv(vec2 deriv) {
            return deriv * u_intensity * 2.0;
        }

        // Based on GDALHillshadeIgorAlg()
        vec4 igor_hillshade(vec2 deriv_in, float azimuth) {
            vec2 deriv = scale_deriv(deriv_in);
            float aspect = get_aspect(deriv);
            float slope_strength = atan(length(deriv)) * 2.0/PI;
            float aspect_strength = 1.0 - abs(mod((aspect + azimuth) / PI + 0.5, 2.0) - 1.0);
            float shadow_strength = slope_strength * aspect_strength;
            float highlight_strength = slope_strength * (1.0-aspect_strength);
            return premul(u_shadowColor) * shadow_strength + premul(u_highlightColor) * highlight_strength;
        }

        // Port of MapLibre's hillshade.fragment.glsl, kept line-for-line comparable. The only
        // deliberate difference: the Mercator scale correction is baked into the normal map by
        // NormalMapBuilder instead of being recomputed per fragment.
        vec4 standard_hillshade(vec2 deriv, float azimuth) {
            // We also multiply the slope by an arbitrary z-factor of 0.625
            float slope = atan(0.625 * length(deriv));
            float aspect = get_aspect(deriv);

            float intensity = u_intensity;

            // We scale the slope exponentially based on intensity, using the position of the
            // maximum return value of the shade function as the exponent
            float base = 1.875 - intensity * 1.75;
            float maxValue = 0.5 * PI;
            float scaledSlope = intensity != 0.5 ? ((pow(base, slope) - 1.0) / (pow(base, maxValue) - 1.0)) * maxValue : slope;

            // The accent color is calculated with the cosine of the slope while the shade color is
            // calculated with the sine, so that the accent color's rate of change eases in while
            // the shade color's eases out.
            float accent = cos(scaledSlope);
            // Both the accent and shade color are multiplied by a clamped intensity value so that
            // intensities >= 0.5 do not additionally affect the color values, while intensity
            // values < 0.5 make the overall color more transparent.
            vec4 accent_color = (1.0 - accent) * premul(u_accentColor) * clamp(intensity * 2.0, 0.0, 1.0);

            float shade = abs(mod((aspect + azimuth) / PI + 0.5, 2.0) - 1.0);
            vec4 shade_color = mix(premul(u_shadowColor), premul(u_highlightColor), shade) * sin(scaledSlope) * clamp(intensity * 2.0, 0.0, 1.0);

            return accent_color * (1.0 - shade_color.a) + shade_color;
        }

        // Based on GDALHillshadeAlg(). 'altitude' is the light's elevation above the horizon.
        vec4 basic_hillshade(vec2 deriv_in, float azimuth, float altitude) {
            vec2 deriv = scale_deriv(deriv_in);
            float cos_az = cos(azimuth);
            float sin_az = sin(azimuth);
            float cos_alt = cos(altitude);
            float sin_alt = sin(altitude);

            float cang = (sin_alt - (deriv.y*cos_az*cos_alt - deriv.x*sin_az*cos_alt)) / sqrt(1.0 + dot(deriv, deriv));

            float shade = clamp(cang, 0.0, 1.0);
            if(shade > 0.5) {
                return premul(u_highlightColor) * (2.0*shade - 1.0);
            }
            return premul(u_shadowColor) * (1.0 - 2.0*shade);
        }

        // Based on GDALHillshadeMultiDirectionalAlg(): four lights at 225/270/315/360 degrees,
        // weighted by aspect, so the user azimuth is unused by design. MapLibre's own version
        // degenerates to plain BASIC for the single light source this layer exposes.
        vec4 multidirectional_hillshade(vec2 deriv_in, float altitude) {
            vec2 deriv = scale_deriv(deriv_in);
            float cos_alt = cos(altitude);
            float sin_alt = sin(altitude);
            float xx_plus_yy = dot(deriv, deriv);

            float shade;
            if (xx_plus_yy == 0.0) {
                shade = clamp(sin_alt, 0.0, 1.0);
            } else {
                float x = deriv.x;
                float y = deriv.y;
                // cos(225 deg) * cos(altitude), shared by the 225 and 315 degree lights
                float c225 = -0.70710678 * cos_alt;
                float val225 = sin_alt + (x - y) * c225;
                float val270 = sin_alt - x * cos_alt;
                float val315 = sin_alt + (x + y) * c225;
                float val360 = sin_alt - y * cos_alt;

                float weight225 = 0.5 * xx_plus_yy - x * y;
                float weight270 = x * x;
                float weight315 = xx_plus_yy - weight225;
                float weight360 = y * y;

                float cang = (max(0.0, val225) * weight225 + max(0.0, val270) * weight270 +
                              max(0.0, val315) * weight315 + max(0.0, val360) * weight360) / (xx_plus_yy * 2.0);
                shade = clamp(cang / sqrt(1.0 + xx_plus_yy), 0.0, 1.0);
            }

            if(shade > 0.5) {
                return premul(u_highlightColor) * (2.0*shade - 1.0);
            }
            return premul(u_shadowColor) * (1.0 - 2.0*shade);
        }

        // Based on GDALHillshadeCombinedAlg()
        vec4 combined_hillshade(vec2 deriv_in, float azimuth, float altitude) {
            vec2 deriv = scale_deriv(deriv_in);
            float cos_az = cos(azimuth);
            float sin_az = sin(azimuth);
            float cos_alt = cos(altitude);
            float sin_alt = sin(altitude);

            float cang = acos(clamp((sin_alt - (deriv.y*cos_az*cos_alt - deriv.x*sin_az*cos_alt)) / sqrt(1.0 + dot(deriv, deriv)), -1.0, 1.0));

            cang = clamp(cang, 0.0, PI/2.0);

            float shade = cang * atan(length(deriv)) * 4.0/PI/PI;
            float highlight = (PI/2.0-cang) * atan(length(deriv)) * 4.0/PI/PI;

            return premul(u_shadowColor)*shade + premul(u_highlightColor)*highlight;
        }

        vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {
            // -normal.xy/normal.z is (dh/dEast, dh/dNorth); y negated again for MapLibre's north-at-v=0 DEM.
            vec2 deriv = vec2(-normal.x, normal.y) / max(normal.z, 0.001);

            // Massif-only extra exaggeration; 1.0 leaves the slope as encoded.
            deriv *= u_exaggeration;

            // u_lightDir = (sin(az), cos(az), -sin(alt)); MapLibre adds PI to the azimuth for every method.
            float azimuth = atan(u_lightDir.x, u_lightDir.y) + PI;
            float altitude = asin(clamp(-u_lightDir.z, -1.0, 1.0));

            if (u_method == BASIC) {
                return basic_hillshade(deriv, azimuth, altitude);
            } else if (u_method == COMBINED) {
                return combined_hillshade(deriv, azimuth, altitude);
            } else if (u_method == IGOR) {
                return igor_hillshade(deriv, azimuth);
            } else if (u_method == MULTIDIRECTIONAL) {
                return multidirectional_hillshade(deriv, altitude);
            }
            // STANDARD (default)
            return standard_hillshade(deriv, azimuth);
        }
    )GLSL";

}

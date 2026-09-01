#include "TileRenderer.h"

#include <vt/RenderStats.h>
#include "components/Options.h"
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
#include "terrain/ElevationManager.h"
#include "utils/Const.h"
#include "utils/Log.h"

#ifdef __ANDROID__
#include <sys/system_properties.h>
#include <cstdlib>
#endif
#include "utils/Const.h"

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
        _labelBlendingSpeed(1.0f),
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

    void TileRenderer::setTerrainRenderOrder(int order) {
        std::lock_guard<std::mutex> lock(_mutex);
        _terrainRenderOrder = order;
    }

    void TileRenderer::setTerrainDepthWriteMode(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);
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
            _vtRenderer.reset();
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
    
    bool TileRenderer::prepareFrame(float deltaSeconds, const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);

        return prepareFrameUnsafe(deltaSeconds, viewState);
    }

    // Caller must hold _mutex. onDrawFrame already does, and _mutex is not recursive.
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
        // The cross-layer drape draws the terrain surface from MapRenderer, BEFORE onDrawFrame
        // sets the view state. Without this the surface is drawn with the previous frame's camera
        // while everything else uses the current one, so the ground lags the buildings by exactly
        // one frame during a pan and snaps into place when the motion stops.
        // Resolved BEFORE the view state below, which carries the brightness a style's
        // view::brightness reads. Same reason as the contact shadows: the drape bake asks whether
        // they are active before onDrawFrame has resolved any lighting, so on the first frame at a
        // camera they baked with intensity 0 - and a cached drape is never re-baked for a uniform
        // change, so they stayed missing until a zoom rebuilt the tiles.
        if (auto options = _options.lock()) {
            ResolvedLighting lighting = resolveLighting(options->getLightOptions(), _styleEnvironment);
            _groundAOIntensity = lighting.buildingAoIntensity;
            _groundAOAttenuation = lighting.buildingAoGroundAttenuation;
            // Same reason as the contact shadows above: the DRAPE BAKE evaluates every colour, and
            // it runs here - before onDrawFrame has resolved any lighting. Set only there, a live
            // colour was baked against the previous frame's light, and a cached drape is never
            // re-baked for it, so the ground simply never followed the sun.
            _resolvedRadiance = lighting.radiance;
            _resolvedBrightness = lighting.brightness;
            _backgroundEmissive = lighting.backgroundEmissive;
            _buildingHeightScale = lighting.buildingHeightScale;
        _buildingHeightViewScale = lighting.buildingHeightViewScale;
            _buildingGrowOnAppear = lighting.buildingGrowOnAppear;
            _buildingFadeOnAppear = lighting.buildingFadeOnAppear;
            // Same reason again, and one step earlier than the rest: the owner reads this BEFORE
            // the layer passes, to decide whether to render the occluder buffer at all.
            _textOcclusionOpacity.store(resolveTextOcclusionOpacity(options->getTerrainOptions(), _styleEnvironment));
        }
        // The cross-layer drape draws the terrain surface from MapRenderer, BEFORE onDrawFrame
        // sets the view state. Without this the surface is drawn with the previous frame's camera
        // while everything else uses the current one, so the ground lags the buildings by exactly
        // one frame during a pan and snaps into place when the motion stops.
        cglib::mat4x4<double> prepareModelViewMat = viewState.getModelviewMat() * cglib::translate4_matrix(cglib::vec3<double>(_horizontalLayerOffset, 0, 0));
        vt::ViewState prepareViewState(viewState.getProjectionMat(), prepareModelViewMat, viewState.getZoom(), viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        prepareViewState.planarProjection = isPlanarProjectionMode();
        prepareViewState.lightBrightness = _resolvedBrightness;
        tileRenderer->setViewState(prepareViewState);
        tileRenderer->setGroundAO(_groundAOIntensity, _groundAOAttenuation);
        tileRenderer->setRadiance(_resolvedRadiance);
        tileRenderer->setBackgroundEmissive(_backgroundEmissive);
        tileRenderer->setBuildingHeight(_buildingHeightScale, _buildingHeightViewScale, _buildingGrowOnAppear, _buildingFadeOnAppear);
        tileRenderer->setLabelOcclusionOpacity(_textOcclusionOpacity.load());
        try {
            _framePrepareResult = tileRenderer->startFrame(deltaSeconds * 3);
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

    int TileRenderer::resolveDrapeResolution(int setting, const ViewState& viewState, const std::shared_ptr<Options>& options) {
        if (setting > 0) {
            return setting;
        }
        // From the SCREEN, not from a constant. The tile LOD refines a tile until it covers at most
        // a 2x2 block of nominal tiles (TileLayer::calculateVisibleTiles, tangram's rule), so
        // 2 * tileDrawSize * pixelScale is the widest any tile ever gets on screen. Baking that
        // many texels is one texel per screen pixel at the LOD's own bound: below it the fill edges
        // stair-step as the camera zooms past the tile's own zoom (the magnified drape texel), and
        // above it the extra texels can never be resolved. Rounded UP to a power of two, since the
        // cache holds one texture size and pools them.
        double tileDrawSize = (options ? options->getTileDrawSize() : 256);
        double edge = 2.0 * tileDrawSize * (viewState.getDPI() / Const::UNSCALED_DPI);
        int size = MIN_DRAPE_RESOLUTION;
        while (size < edge && size < MAX_DRAPE_RESOLUTION) {
            size *= 2;
        }
        // ... and then what MEMORY allows, which is the binding constraint: the rule above asks for
        // 1024 on the Crosscall, and a drape texture at 1024 x 1024 x RGBA is 4 MB PER TILE, so the
        // cache's 160 entries would be 640 MB. What the device does with that is thrash - measured
        // on the north pan, the drape section is 13.4 ms at 1024 and 5.2 ms at 512, for a
        // difference the screen cannot show once the texture is mipmapped.
        // So: the largest power of two at which a working cover still fits the cache's budget.
        std::size_t bytesPerTile = static_cast<std::size_t>(size) * size * 4;
        while (TerrainDrapeCache::isBudgetEnabled() && size > MIN_DRAPE_RESOLUTION && bytesPerTile * DRAPE_WORKING_SET > TerrainDrapeCache::MAX_BYTES) {
            size /= 2;
            bytesPerTile = static_cast<std::size_t>(size) * size * 4;
        }
        return size;
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

    bool TileRenderer::calculateShadowViewProj(const std::vector<vt::TileId>& tileIds, const std::vector<vt::TileId>& casterTileIds, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<vt::TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            // What the fit falls back on when no tile carries a DEM: the same factor the elevation
            // cache would report, minus the exaggeration a flat map does not have. Without it a 2D
            // map failed the fit outright and drew no shadow at all.
            tileRenderer->setMetersToInternal(Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE);
            return tileRenderer->calculateShadowViewProj(tileIds, casterTileIds, sunDir, tileHeights, minHeight, maxHeight, distanceFactor, cameraDistance, mapSize, cascade, cascadeCount, boxCasterTileIds, depthRangeMeters, texelMeters, lightViewProj);
        }
        return false;
    }

    float TileRenderer::shadowCasterFadeSignature() const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->shadowCasterFadeSignature();
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

    bool TileRenderer::isGroundAOBakeable() const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->isGroundAOBakeable();
        }
        return false;
    }

    void TileRenderer::setLabelOcclusionDepth(unsigned int depthTexture, float occluderSize) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setLabelOcclusionDepth(depthTexture, occluderSize);
        }
    }

    bool TileRenderer::isLabelOcclusionWanted() const {
        if (_textOcclusionOpacity.load() < 1.0f) {
            return true;
        }
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->hasStyledLabelOcclusion();
        }
        return false;
    }

    int TileRenderer::renderLabelOcclusionDepth() {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            return tileRenderer->renderLabelOcclusionDepth();
        }
        return 0;
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

    void TileRenderer::setTerrainShadowMap(unsigned int texture, int mapSize, int cascades, const std::array<float, 4>& depthBiases, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, 4>& lightViewProjs) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = (_vtRenderer ? _vtRenderer->getTileRenderer() : std::shared_ptr<vt::GLTileRenderer>())) {
            tileRenderer->setTerrainShadowMap(static_cast<GLuint>(texture), mapSize, cascades, depthBiases, strength, softness, depthTexture, hardwarePCF, normalOffset, sunDir, lightViewProjs);
        }
    }

    cglib::vec3<float> TileRenderer::linearColor(const Color& color, float intensity) {
        auto linear = [intensity](unsigned char c) { return std::pow(c / 255.0f, 2.2f) * intensity; };
        return cglib::vec3<float>(linear(color.getR()), linear(color.getG()), linear(color.getB()));
    }

    vt::GLTileRenderer::TerrainLighting TileRenderer::buildTerrainLighting(const ResolvedLighting& lighting) {
        vt::GLTileRenderer::TerrainLighting terrainLighting;
        terrainLighting.enabled = true;
        // A style whose 2D colours already carry the light is lit NEUTRALLY rather than not at all.
        // The ground's shadow multiply lives INSIDE the terrain shading block (applyTerrainShading),
        // so switching that block off takes the shadow with it - the caster pass keeps running and
        // nothing receives. White ambient at full weight makes the lit term exactly 1, which leaves
        // the authored colour alone and lets the shadow through.
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
            tileRenderer->setTerrainLighting(terrainLighting);
        }
    }

    void TileRenderer::setTerrainPaintTiles(const std::vector<vt::TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

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

    // Measurement switches, all off by default:
    //   debug.massif.groundpaint 1  paint drawn AS the ground (tangram) - one draw per tile cheaper,
    //                              but the shading goes under every ground-shaped fill
    //   debug.massif.demtaps 4      elevation fetches per terrain vertex (16 lattice clamp / 4
    //                              manual bilinear / 1 hardware-filtered, tangram's) - first
    //                              suspect whenever the frame sits in the swap wait
    //   debug.massif.tilebg 1       per-layer per-tile background meshes tangram does not have
#ifdef __ANDROID__
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

    // The stencil tile masks that clip each tile's content to its own screen footprint, forced on
    // (1) or off (0) instead of the renderer's own rule - which drops them in a terrain frame,
    // where a mask is a full displaced grid per tile, and keeps them in 2D, where it is a
    // two-triangle quad. Tangram has no stencil anywhere. What they protect against is a retained
    // (proxy) tile painting through the gaps of the tile that replaced it, so the A/B to run is
    // the zoom transitions, not only the frame rate.
    //   adb shell setprop debug.massif.tilemasks 1
#ifdef __ANDROID__
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

#ifdef __ANDROID__
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

#ifdef __ANDROID__
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
        std::lock_guard<std::mutex> lock(_mutex);

        if (!initializeRenderer()) {
            return false;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return false;
        }

        // vt has no logger of its own, so the fallback is invisible without this. It means a
        // program would not build at '#version 300 es' and was rebuilt at 1.00 - the map still
        // draws, which is exactly why it needs saying out loud.
        if (!_essl3FallbackReported && tileRenderer->hasShaderVersionFallback()) {
            _essl3FallbackReported = true;
            Log::Warn("TileRenderer: a shader fell back from GLSL ES 3.00 to 1.00");
        }

        cglib::mat4x4<double> modelViewMat = viewState.getModelviewMat() * cglib::translate4_matrix(cglib::vec3<double>(_horizontalLayerOffset, 0, 0));
        vt::ViewState vtViewState(viewState.getProjectionMat(), modelViewMat, viewState.getZoom(), viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        vtViewState.planarProjection = isPlanarProjectionMode(); // labels rescale by view depth, so neither terrain elevation nor a tilt blows up their screen size
        vtViewState.lightBrightness = _resolvedBrightness; // a style's view::brightness, so an emissive ramp over it follows the hour
        vtViewState.focusDistance = static_cast<float>(cglib::length(viewState.getCameraPos() - viewState.getFocusPos())); // what the zoom sizes labels at; vt guesses it from the ground plane otherwise
        tileRenderer->setViewState(vtViewState);
        // A line width is given in unscaled-DPI units; this is what one of them is worth in device
        // pixels, so the antialias ramp can be one pixel wide instead of one unit (see lineFsh).
        tileRenderer->setLineAntialiasScale(viewState.getNormalizedResolution() > 0 ? viewState.getHeight() / viewState.getNormalizedResolution() : 1.0f);
        tileRenderer->setInteractionMode(_interactionMode);
        tileRenderer->setRasterFilterMode(_rasterFilterMode);
        tileRenderer->setLayerBlendingSpeed(_layerBlendingSpeed);
        tileRenderer->setLabelBlendingSpeed(_labelBlendingSpeed);
        tileRenderer->setRendererLayerFilter(_rendererLayerFilter);

        // Terrain state: enable depth-based terrain rendering and rebuild tile surfaces
        // when the elevation data changes (new DEM tiles, exaggeration change). The rebuild
        // is debounced: during the initial load a new elevation tile may arrive almost every
        // frame and rebuilding all surfaces each time would kill interactivity.
        bool terrainMode = false;
        float terrainDepthBias = 0.0f;
        std::shared_ptr<TerrainOptions> activeTerrainOptions;
        if (auto options = _options.lock()) {
            if (options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
                if (auto terrainOptions = options->getTerrainOptions()) {
                    if (terrainOptions->isActive()) {
                        terrainMode = true;
                        // Tile geometry lies exactly on the terrain surfaces (same transformer and
                        // tesselation), so it only needs a small equality slack - the slope-scaled
                        // polygon offset in the vt renderer provides the distance-stable pull
                        // towards the viewer. A large constant clip-space bias would translate to
                        // hundreds of meters of depth tolerance at far distances (see-through ridges).
                        terrainDepthBias = terrainOptions->getDepthBias() * 0.1f;
                        activeTerrainOptions = terrainOptions;
                        const std::shared_ptr<ElevationManager>& elevationManager = terrainOptions->getElevationManager();
                        unsigned int elevationVersion = elevationManager->getVersion();
                        if (elevationVersion != _elevationVersion) {
                            auto now = std::chrono::steady_clock::now();
                            // Drop only the surfaces over the tiles that changed; the global reset
                            // is the fallback for whole-data-set changes and change-log overflow.
                            // A scale-only change (an exaggeration ramp) leaves the DATA version
                            // alone - the surfaces displace on the GPU, only label anchors go stale.
                            unsigned int elevationDataVersion = elevationManager->getDataVersion();
                            bool scaleOnly = (_elevationDataVersion != 0 && elevationDataVersion == _elevationDataVersion);
                            _elevationDataVersion = elevationDataVersion;

                            std::vector<MapTile> changedTiles;
                            if (scaleOnly) {
                                _elevationVersion = elevationVersion;
                                tileRenderer->invalidateLabelElevation();
                            } else if (_elevationVersion != 0 && elevationManager->getChangedTiles(_elevationVersion, changedTiles)) {
                                _elevationVersion = elevationVersion;
                                std::vector<vt::TileId> changedTileIds;
                                changedTileIds.reserve(changedTiles.size());
                                for (const MapTile& changedTile : changedTiles) {
                                    changedTileIds.emplace_back(changedTile.getZoom(), changedTile.getX(), changedTile.getY());
                                }
                                tileRenderer->invalidateTileSurfaces(changedTileIds);
                                // Labels are anchored onto the terrain the same way, and at one
                                // elevation sample per label vertex a blanket re-anchor of the
                                // visible label set costs several hundred milliseconds - the
                                // same targeted list keeps it to the labels actually affected.
                                tileRenderer->invalidateLabelElevation(changedTileIds);
                            } else if (!_lastSurfaceResetTime || now - *_lastSurfaceResetTime > std::chrono::milliseconds(SURFACE_RESET_DELAY)) {
                                _elevationVersion = elevationVersion;
                                _lastSurfaceResetTime = now;
                                tileRenderer->resetTileSurfaces();
                                tileRenderer->invalidateLabelElevation();
                            } else if (auto mapRenderer = _mapRenderer.lock()) {
                                mapRenderer->requestRedraw(); // apply the pending rebuild on a later frame
                                // This path asks for a frame without drawing anything new. It is
                                // meant to be a handful of frames while a rebuild is debounced; if
                                // the elevation version never settles it is an endless render loop
                                // instead, so say so rather than leaving it to be inferred from the
                                // battery.
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
        // GPU terrain draping: provide elevation textures so that draped geometry is
        // displaced in the vertex shader - every layer samples the same textures, so all
        // layers agree on heights exactly. Requires vertex texture fetch support;
        // without it the CPU displacement path with polygon offsets stays active.
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
                    // Cap elevation levels at what the mesh can express - for every elevation
                    // consumer, not just the drawn surface (billboard occlusion ray marching and
                    // element placement query the same manager and must see the same heights).
                    elevationManager->setSurfaceResolution(activeTerrainOptions->getMeshResolution());
                }
                if (_elevationTextureCache && _elevationTextureCache->getElevationManager() != elevationManager) {
                    _elevationTextureCache.reset();
                }
                if (!_elevationTextureCache && elevationManager) {
                    if (auto mapRenderer = _mapRenderer.lock()) {
                        _elevationTextureCache = std::make_shared<ElevationTextureCache>(elevationManager, mapRenderer->getGLResourceManager());
                    }
                }
                if (_elevationTextureCache) {
                    // The paint reads the elevation texture per FRAGMENT, so it may ignore the
                    // mesh's level cap. A dial, not a flag - each level back is 4x the working set.
                    //   adb shell setprop debug.massif.paintdetail 0|1|2   (2 = the source's own level)
                    _elevationTextureCache->setDetailLevels(_terrainPaintEnabled && _terrainPaintFullDetail ? terrainPaintDetailLevels() : 0);
                    _elevationTextureCache->beginFrame();
                    std::shared_ptr<ElevationTextureCache> elevationTextureCache = _elevationTextureCache;
                    terrainTextureProvider = [elevationTextureCache](const vt::TileId& tileId, vt::GLTileRenderer::TerrainTexture& terrainTexture) {
                        return elevationTextureCache->getTexture(tileId, terrainTexture);
                    };
                    // Every terrain tile layer works in its own depth domain (the vt
                    // renderer clears the depth buffer and renders its reference surface
                    // pre-pass before its content, which then WRITES its real depth -
                    // tangram-style). Cross-layer stacking is pure painter's order, so no
                    // per-layer depth stride is needed - and any constant-NDC stride
                    // would shift the final depth domain away from what vector elements
                    // depth-test against after the tile layers.
                    terrainDepthBias = 0.0f;
                }
            }
        }
        tileRenderer->setTerrainTextureProvider(terrainTextureProvider);
        if (terrainMode && activeTerrainOptions) {
            // Labels are anchored when their tile is decoded, possibly before elevation
            // data arrives - re-anchor them whenever the elevation version changes
            std::shared_ptr<ElevationManager> elevationManager = activeTerrainOptions->getElevationManager();
            tileRenderer->setLabelElevationProvider([elevationManager](const cglib::vec3<double>& pos) {
                return elevationManager->getDisplayHeight(pos(0), pos(1), ElevationManager::LoadMode::CACHED_ONLY);
            });
        } else {
            tileRenderer->setLabelElevationProvider(std::function<double(const cglib::vec3<double>&)>());
        }
        tileRenderer->setTerrainMode(terrainMode, terrainDepthBias);
        tileRenderer->setTileMasks(tileMasksMode());
        // The geometry-vs-surface chord error shrinks quadratically with the mesh
        // resolution (both the tile surfaces and the draped geometry tesselate to
        // tileMeters/meshResolution cells), so the depth slack can shrink with it.
        // The default resolution 32 maps to factor 1 (the calibrated slack).
        float terrainSlackScale = 1.0f;
        if (terrainMode && activeTerrainOptions) {
            float resolutionRatio = 32.0f / std::max(32, activeTerrainOptions->getMeshResolution());
            terrainSlackScale = resolutionRatio * resolutionRatio;
        }
        tileRenderer->setTerrainSlackScale(terrainSlackScale);
        // Tangram's model: one shared grid surface reused for every tile, and painter-order depth
        // on top of it (the surface is the bottom painter layer, no occluder pre-pass, no slack).
        // Needs GPU draping - a GPU without vertex texture fetch falls back to adaptive tesselation.
        bool regularGrid = terrainMode && activeTerrainOptions && (bool) terrainTextureProvider;
        tileRenderer->setTerrainRegularGrid(regularGrid, activeTerrainOptions ? activeTerrainOptions->getMeshResolution() : 0);
        // Maplibre-style RTT draping. It requires the shared regular grid: the drape UV is the
        // grid's tile-local [0,1] vertex position, which only the regular grid provides.
        bool drapeFills = regularGrid && activeTerrainOptions->isDrapeFillsEnabled();
        // Tangram's content depth shift (res/scenes/terrain-3d.yaml, a flat 0.02), verbatim and
        // unscaled: it separates COPLANAR STYLE LAYERS, one step each - it is not a budget to spend,
        // and scaling it up is what let far content over a near ridge. An un-subdivided fill gets
        // its clearance from the geometry-sized slack instead. docs/internals/rendering/05-depth-model.md.
        //   adb shell setprop debug.massif.depthshift <value>   (measurement override)
        float contentDepthShift = getTerrainContentDepthShift();
        if (_terrainGroundActive && contentDepthShift == 0.0f) {
            contentDepthShift = TERRAIN_TANGRAM_DEPTH_SHIFT;
        }
        tileRenderer->setTerrainContentDepthShift(contentDepthShift);
        // Metre-constant clearance for draped LINES over the shared ground (see applyDepthBias in
        // vt). A line chords over the relief between its own vertices; under a ground that writes
        // depth that sag is what cuts roads and contours into fragments. The quantity is metres of
        // sag, so the clearance is expressed in metres and converted at the equator scale - the
        // remaining 1/cos(latitude) is under a factor of 1.5 at the latitudes terrain is used at,
        // which is inside the tolerance this is tuned to anyway.
        //   adb shell setprop debug.massif.lineclearance <metres>
        tileRenderer->setTerrainLineClearance(static_cast<float>(terrainLineClearanceMeters() * Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE));
        tileRenderer->setTerrainEdgeStitching(regularGrid && activeTerrainOptions && activeTerrainOptions->isTileEdgeStitchingEnabled());
        // Draped content is baked FLAT (orthographic, no displacement), so lines need no terrain
        // subdivision either - draping them is strictly cheaper as well as artifact-free. It is
        // also the drape texture's resolution though: a line baked into it is magnified with the
        // texture, which turns dense thin lines (contours on a steep slope) into a blurred wash.
        // DrapeLines is what trades the one for the other, and TileLayer already decodes lines at
        // source density / subdivided to match it.
        bool drapeLines = drapeFills && activeTerrainOptions && activeTerrainOptions->isDrapeLinesEnabled();
        tileRenderer->setTerrainDrapeFills(drapeFills, drapeLines);
        // ...except the layers the application keeps sharp (contours by default), drawn live instead.
        //   adb shell setprop debug.massif.nodrapelayers "^contour.*" ("none" drapes everything)
        tileRenderer->setNoDrapeLayerFilter(noDrapeLayerFilter(
            activeTerrainOptions ? activeTerrainOptions->getNoDrapeLayerFilter() : std::string()));
        tileRenderer->setTerrainDrapeResolution(resolveDrapeResolution(activeTerrainOptions ? activeTerrainOptions->getDrapeResolution() : 0, viewState, _options.lock()));
        // Sun lighting of the draped surface. Once every 2D layer is baked into the drape
        // texture the surface is the only lit ground geometry in the scene, so the whole map
        // is shaded by one directional light that follows the time of day - and the pre-baked
        // hillshade raster layer becomes optional rather than the only way to get relief.
        vt::GLTileRenderer::TerrainLighting terrainLighting;
        if (auto options = _options.lock()) {
            // The style's values win over the options wherever it has an opinion; the rest of the
            // sun stays with LightOptions. Both are re-read every frame, so either may depend on
            // the zoom.
            ResolvedLighting lighting = resolveLighting(options->getLightOptions(), _styleEnvironment);
            // Extrusions light by their OWN resolved pair, so a style can tune the walls without
            // moving the terrain sun. Captured for the 3D lighting callback, which runs at DRAW
            // time and cannot resolve anything itself.
            _buildingLightIntensity = lighting.buildingLightIntensity;
            _buildingAmbient = lighting.buildingAmbient;
            _buildingVerticalGradient = lighting.buildingVerticalGradient;
            _buildingRoofShade = lighting.buildingRoofShade;
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
            _buildingEmissive = lighting.buildingEmissive;
            _backgroundEmissive = lighting.backgroundEmissive;
            _resolvedRadiance = lighting.radiance;
            _resolvedBrightness = lighting.brightness;
            // The terrain surface is what this lights, and it exists whenever the stack draws one:
            // baked under a drape, or the shared ground pass when the drape is off. Gating on the
            // drape alone left the ground AND the hillshade paint over it unlit - and with them the
            // shadow map, since the shadow multiplies the lit colour (the paint is drawn from this
            // layer's own pass, which runs after the owner has set the stack's sun, so it saw the
            // value this line computes).
            // A pre-lit style is handled inside buildTerrainLighting, which lights it neutrally so
            // the ground keeps its authored colour and still receives the shadow. Measured at the
            // Opera, dusk: (68,64,83) against gl-js's (69,64,83); lit twice it was (14,16,31).
            if ((drapeFills || _terrainGroundActive) && lighting.terrainLightingEnabled) {
                terrainLighting = buildTerrainLighting(lighting);
            }

            // Distance fog, lit by the same sun as the ground (see resolveFog). The range is
            // camera-relative, so resolveFog already returns internal units - it needs no terrain,
            // and this is what fogs a plain 2D map as well.
            ResolvedFog fog = resolveFog(options->getFogOptions(), _styleEnvironment, lighting, viewState.calculateCameraDistance());
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
        // The terrain base fill (color or the map background bitmap) is rendered
        // globally by MapRenderer BEFORE all tile layers, so it stays visible behind
        // translucent tile layer content regardless of the layer stacking order.
        // The per-layer surface pre-pass here stays depth-only.
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
                // Compass azimuth (0 = north, clockwise) of the horizontal part, counter-rotated by
                // the map rotation so the light stays anchored to the viewport. The horizontal
                // length is preserved - the previous acos(y) form assumed a unit xy and silently
                // rewrote the horizontal/vertical balance of any other direction.
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
            refresh = prepareFrameUnsafe(deltaSeconds, viewState);

            tileRenderer->renderGeometry(true, false);
            if (_labelOrder == 0) {
                tileRenderer->renderLabels(true, false);
            }
            if (_buildingOrder == 0) {
                tileRenderer->renderGeometry(false, true);
            }
            if (_labelOrder >= 0 && drawsBillboardLabelsHere(0)) {
                tileRenderer->renderLabels(false, true);
            }
        }
        catch (const std::exception& ex) {
            Log::Errorf("TileRenderer::onDrawFrame: Rendering failed: %s", ex.what());
        }
    
        // Reset GL state to the expected state
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        GLContext::CheckGLError("TileRenderer::onDrawFrame");
        return refresh;
    }
    
    bool TileRenderer::onDrawFrame3D(float deltaSeconds, const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);

        // The frame ends here regardless of what follows, so clear the prepare latch up front:
        // leaking it past an early return would make every later frame skip startFrame.
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
                // Inline: the extrusions are the last tile content of the frame, so they can be
                // drawn straight into the main framebuffer (tangram's way) instead of through the
                // per-layer 3D overlay - nothing after them depth-tests against what they write.
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

        // Reset GL state to the expected state
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        GLContext::CheckGLError("TileRenderer::onDrawFrame3D");
        return refresh;
    }
    
    bool TileRenderer::cullLabels(vt::LabelCuller& culler, const ViewState& viewState) {
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
        vt::ViewState cullViewState(viewState.getProjectionMat(), modelViewMat, viewState.getZoom(),
viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        cullViewState.planarProjection = isPlanarProjectionMode(); // keep culling envelopes consistent with the rendered label sizes
        cullViewState.lightBrightness = _resolvedBrightness;
        cullViewState.focusDistance = static_cast<float>(cglib::length(viewState.getCameraPos() - viewState.getFocusPos()));
        culler.setViewState(cullViewState);

        try {
            tileRenderer->cullLabels(culler);
        }
        catch (const std::exception& ex) {
            Log::Errorf("TileRenderer::cullLabels: Culling failed: %s", ex.what());
            return false;
        }
        return true;
    }
    
    bool TileRenderer::refreshTiles(const std::vector<std::shared_ptr<TileDrawData> >& drawDatas) {
        // Timed separately from the work: this runs inside the layer draw pass, and the tile
        // threads hold this mutex while storing decoded tiles - which is exactly when the set
        // changes. A long wait here and a short one inside setVisibleTiles mean different fixes.
        VT_STAT_CLOCK(refreshClock);
        std::lock_guard<std::mutex> lock(_mutex);
        VT_STAT_SPLIT(refreshTilesLockNs, refreshClock);

        std::map<vt::TileId, std::shared_ptr<const vt::Tile> > tiles;
        for (const std::shared_ptr<TileDrawData>& drawData : drawDatas) {
            tiles[drawData->getVTTileId()] = drawData->getVTTile();
        }

        bool changed = (tiles != _tiles) || (_horizontalLayerOffset != 0);
        if (!changed) {
            return false;
        }

        if (_vtRenderer) {
            if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer()) {
                if (_horizontalLayerOffset != 0) {
                    tileRenderer->teleportVisibleTiles((int)std::round(_horizontalLayerOffset / Const::WORLD_SIZE), 0);
                }
                tileRenderer->setVisibleTiles(tiles);
            }
        }
        _tiles = std::move(tiles);
        _horizontalLayerOffset = 0;
        // The changed path only - the unchanged one returns above and costs nothing. INCLUDES
        // setVisibleTiles, whose own splits break it down further.
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

        // Tile geometry is built flat in terrain mode (heights are applied on the GPU):
        // pre-intersect the ray with the terrain surface and pick vertically below the hit
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

    Color TileRenderer::evaluateColorFunc(const vt::ColorFunction& colorFunc, const ViewState& viewState) {
        cglib::mat4x4<double> modelViewMat = viewState.getModelviewMat();
        vt::ViewState vtViewState(viewState.getProjectionMat(), modelViewMat, viewState.getZoom(),
viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        return Color(colorFunc(vtViewState).value());
    }

    void TileRenderer::setStyleEnvironment(const StyleEnvironment& env) {
        std::lock_guard<std::mutex> lock(_mutex);

        _styleEnvironment = env;
    }

    float TileRenderer::evaluateFloatFunc(const vt::FloatFunction& floatFunc, const ViewState& viewState) {
        cglib::mat4x4<double> modelViewMat = viewState.getModelviewMat();
        vt::ViewState vtViewState(viewState.getProjectionMat(), modelViewMat, viewState.getZoom(), viewState.getRotation(), viewState.getTilt(), viewState.getAspectRatio(), viewState.getNormalizedResolution());
        return floatFunc(vtViewState);
    }

    float TileRenderer::getTerrainContentDepthShift() {
#ifdef __ANDROID__
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
#ifdef __ANDROID__
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

#ifdef __ANDROID__
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
#ifdef __ANDROID__
        // adb shell setprop debug.massif.paintdetail 0|1|2 - elevation levels beyond the mesh cap.
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
        // The label size correction and the pixel-grid snapping belong to the PROJECTION, not to
        // the terrain: a tilted flat map divides by w exactly the same way, which is what made
        // labels near the camera far larger than the ones behind them.
        if (auto options = _options.lock()) {
            return options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR;
        }
        return false;
    }

    void TileRenderer::updateLabelOcclusionTest(const std::shared_ptr<vt::GLTileRenderer>& tileRenderer, const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions) {
        if (!terrainOptions || !terrainOptions->isBillboardOcclusionEnabled()) {
            _labelOcclusionState.reset();
            tileRenderer->setLabelOcclusionTest(std::function<bool(const cglib::vec3<double>&)>());
            return;
        }

        // Preferred path: pixel-exact occlusion against the read-back terrain depth buffer
        // (rendered by MapRenderer each frame) - matches what is actually on screen and is
        // much cheaper than ray-marching the elevation grids per label.
        if (auto mapRenderer = _mapRenderer.lock()) {
            if (mapRenderer->getTerrainRenderer() != nullptr) {
                {
                    _labelOcclusionState.reset();
                    std::weak_ptr<MapRenderer> mapRendererWeak = _mapRenderer;
                    // The tolerance is relative to distance: at its default it only absorbs the
                    // mismatch between the anchor and the terrain it sits on, and raising it lets
                    // partly hidden features label (the peak-finder case). The projection itself
                    // belongs to the depth buffer's own camera, so it lives with the buffer.
                    float occlusionTolerance = 1.0f + std::max(MIN_OCCLUSION_TOLERANCE, terrainOptions->getBillboardOcclusionTolerance());
                    tileRenderer->setLabelOcclusionTest([mapRendererWeak, occlusionTolerance](const cglib::vec3<double>& pos) {
                        auto mapRenderer = mapRendererWeak.lock();
                        if (!mapRenderer) {
                            return false;
                        }
                        TerrainRenderer* terrainRenderer = mapRenderer->getTerrainRenderer();
                        if (!terrainRenderer) {
                            return false;
                        }
                        return terrainRenderer->isOccludedByTerrain(pos, occlusionTolerance);
                    });
                    return;
                }
            }
        }

        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
        if (!_labelOcclusionState) {
            _labelOcclusionState = std::make_shared<LabelOcclusionState>();
        }
        std::shared_ptr<LabelOcclusionState> state = _labelOcclusionState;

        // Invalidate cached results when the camera moves significantly or the elevation data changes
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

        // The ray path lifts the target above the anchor by the same relative tolerance, so
        // both occlusion paths answer the same question.
        double rayTolerance = 0.005 + 0.5 * terrainOptions->getBillboardOcclusionTolerance();
        tileRenderer->setLabelOcclusionTest([state, elevationManager, cameraPos, rayTolerance](const cglib::vec3<double>& pos) -> bool {
            // Quantize the position for caching (roughly 4m grid)
            const double QUANT = 10.0;
            long long key = (static_cast<long long>(pos(0) * QUANT) * 73856093LL) ^ (static_cast<long long>(pos(1) * QUANT) * 19349663LL) ^ (static_cast<long long>(pos(2) * QUANT) * 83492791LL);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                auto it = state->results.find(key);
                if (it != state->results.end()) {
                    return it->second;
                }
            }

            double dist = cglib::length(pos - cameraPos);
            cglib::vec3<double> target = pos + cglib::vec3<double>(0, 0, dist * rayTolerance);
            cglib::ray3<double> ray(cameraPos, target - cameraPos);
            double t = 0;
            bool occluded = elevationManager->intersectRay(ray, t) && t > 0 && t < 0.995;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->results[key] = occluded;
            }
            return occluded;
        });
    }

    bool TileRenderer::initializeRenderer() {
        if (_vtRenderer && _vtRenderer->isValid()) {
            return true;
        }

        std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock();
        if (!mapRenderer) {
            return false; // safety check, should never happen
        }

        // Null once the surface is gone - a frame still in flight has nothing to create into (#178).
        std::shared_ptr<GLResourceManager> glResourceManager = mapRenderer->getGLResourceManager();
        if (!glResourceManager) {
            return false;
        }

        Log::Debug("TileRenderer: Initializing renderer");
        _vtRenderer = glResourceManager->create<VTRenderer>(_tileTransformer);

        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer()) {
            tileRenderer->setVisibleTiles(_tiles);

            if (!std::dynamic_pointer_cast<PlanarProjectionSurface>(mapRenderer->getProjectionSurface())) {
                vt::GLTileRenderer::LightingShader lightingShader2D(true, LIGHTING_SHADER_2D, [this](GLuint shaderProgram, const vt::ViewState& viewState) {
                    glUniform3fv(glGetUniformLocation(shaderProgram, "u_viewDir"), 1, _viewDir.data());
                });
                tileRenderer->setLightingShader2D(lightingShader2D);
            }

            // The RESOLVED sun (style over LightOptions), captured by onDrawFrame: this callback runs
            // at draw time and cannot resolve it itself. Same four values the terrain surface is lit
            // by, so a building and the ground it stands on agree about the hour.
            // Per FRAGMENT: the shadow term has to reach the lighting, and it only exists there.
            vt::GLTileRenderer::LightingShader lightingShader3D(false, LIGHTING_SHADER_3D, [this](GLuint shaderProgram, const vt::ViewState& viewState) {
                // Linear, and already carrying the intensity: the shader sums the two and returns
                // the sum to sRGB once, so it never needs the intensities apart.
                cglib::vec3<float> sunColor = linearColor(_resolvedSunColor, _buildingLightIntensity);
                cglib::vec3<float> ambientColor = linearColor(_resolvedAmbientColor, _buildingAmbient);
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_sunDir"), 1, _resolvedBuildingSunDir.data());
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_sunColor"), 1, sunColor.data());
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_ambientColor"), 1, ambientColor.data());
                glUniform2f(glGetUniformLocation(shaderProgram, "u_verticalGradient"), _buildingVerticalGradient, _buildingRoofShade);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_emissive"), _buildingEmissive);
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_radiance"), 1, _resolvedRadiance.data());
            });
            tileRenderer->setLightingShader3D(lightingShader3D);

            vt::GLTileRenderer::LightingShader lightingShaderNormalMap(false, _normalMapLightingShader, [this](GLuint shaderProgram, const vt::ViewState& viewState) {
                    // Straight (non-premultiplied) colors - the shader premultiplies them before
                    // mixing, which is the form MapLibre's hillshade fragment shader works in.
                    glUniform4f(glGetUniformLocation(shaderProgram, "u_shadowColor"), _normalMapShadowColor.getR() / 255.0f, _normalMapShadowColor.getG() / 255.0f, _normalMapShadowColor.getB() / 255.0f, _normalMapShadowColor.getA() / 255.0f);
                    glUniform4f(glGetUniformLocation(shaderProgram, "u_accentColor"), _normalMapAccentColor.getR() / 255.0f, _normalMapAccentColor.getG() / 255.0f, _normalMapAccentColor.getB() / 255.0f, _normalMapAccentColor.getA() / 255.0f);
                    glUniform4f(glGetUniformLocation(shaderProgram, "u_highlightColor"), _normalMapHighlightColor.getR() / 255.0f, _normalMapHighlightColor.getG() / 255.0f, _normalMapHighlightColor.getB() / 255.0f, _normalMapHighlightColor.getA() / 255.0f);
                    glUniform3fv(glGetUniformLocation(shaderProgram, "u_lightDir"), 1, _normalLightDir.data() );
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_method"), (_hillshadeMethod));
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_exaggeration"), _hillshadeExaggeration);
                    // MapLibre's 'hillshade-exaggeration' (the slope response curve), fed from the
                    // layer's contrast. Kept separate from u_exaggeration, which scales the slope.
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_intensity"), _hillshadeIntensity);
                    // Elevation-encoded normal map + contour lines (opt-in). These uniforms have no
                    // effect unless the normal map was built with elevation encoding (see HillshadeRasterTileLayer).
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_elevationEncoded"), _normalMapElevationEncoded ? 1.0f : 0.0f);
                    glUniform2f(glGetUniformLocation(shaderProgram, "u_elevationDecode"), vt::NormalMapBuilder::ELEVATION_SCALE, vt::NormalMapBuilder::ELEVATION_OFFSET);
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_contrast"), _hillshadeIntensity);
                    glUniform4f(glGetUniformLocation(shaderProgram, "u_contourColor"), _normalMapContourColor.getR() / 255.0f, _normalMapContourColor.getG() / 255.0f, _normalMapContourColor.getB() / 255.0f, _normalMapContourColor.getA() / 255.0f);
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_contourInterval"), _normalMapContourInterval);
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_contourWidth"), _normalMapContourWidth);
                    // Current fractional map zoom, for per-zoom custom normal-map shaders (getMapZoom()).
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_zoom"), viewState.zoom);
            });
            tileRenderer->setLightingShaderNormalMap(lightingShaderNormalMap);
        }

        return _vtRenderer && _vtRenderer->isValid();
    }

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
        // How much of the colour is EMITTED rather than lit (mapbox's *-emissive-strength): 1 draws
        // it as authored whatever the hour, 0 hands it entirely to the scene light.
        uniform float u_emissive;
        // What the light does to a flat, upward-facing surface (calculateGroundRadiance), in LINEAR
        // space. Passed even though the 3D pass computes its own per-face term, because it is what
        // a replaceable grade is written against and what the emissive mixes back towards.
        uniform vec3 u_radiance;
        vec4 applyLighting3D(lowp vec4 color, mediump vec3 normal, mediump float wallT, mediump float sideVertex, mediump float shadow, mediump float skyShadow) {
            // Ambient occlusion where a wall meets the ground: that corner is shadowed by the ground
            // and by the building's own footprint whatever the sun does, and it is the cue that
            // makes an extrusion stand on the terrain instead of floating over it - the shadow map
            // cannot resolve it, its texels are metres wide.
            //
            // wallT is the ramp itself, baked per vertex by the tesselator from the wall's ABSOLUTE
            // height and the style's reach (TileLayerBuilder::packGradientT). Both are style values
            // in one unit there, where the shader's own height carries a packing and a tile scale;
            // and being absolute, every part of a building shares one ramp instead of restarting.
            // sideVertex weights it: 1 on a wall, 0 on a roof, and partway on the bevel that
            // rounds the edge, so the gradient fades out as the surface turns to face up.
            // The roof shade rides the same weight: full on a roof, none on a wall, and partway
            // across the bevel, so a darkened roof does not meet its wall on a hard line.
            lowp vec3 baseColor = color.rgb * mix(u_verticalGradient.y, mix(1.0 - u_verticalGradient.x, 1.0, wallT), sideVertex);
            // Mapbox's fill-extrusion model (docs/internals/rendering/08-lighting-sky-fog.md).
            // Ambient and sun simply SUM - no headroom coupling - and the ambient itself is
            // direction-aware, which is what separates wall tones without any gradient ramp.
            mediump float ndl = dot(normal, u_sunDir);
            // CLAMPED, which is what fill_extrusion does (shadowed_light_factor_normal,
            // _prelude_shadow.fragment.glsl). The WRAPPED form next to it, calculate_NdotL, is for
            // model layers - reaching for it here flattens the whole point: a low dawn sun should
            // leave a roof (N.L = sin(altitude)) well under the wall facing it, and wrapping lifts
            // the roof by half the gap. The nuance between the walls comes from the ambient below.
            // Faded out as the sun crosses the horizon: a wall's normal has no z, so N.L stays
            // positive with the sun BELOW the map and a night facade was lit from underground.
            // The ground needs no such term - its normal points up, so N.L closes on its own.
            mediump float sunNdl = max(0.0, ndl) * smoothstep(-0.035, 0.0, u_sunDir.z);
            // Sky is brighter near the sun: faces turned away lose up to 30% of the ambient,
            // scaled by how bright the sun actually is.
            mediump float dirLuminance = dot(u_sunColor, vec3(0.2126, 0.7152, 0.0722));
            mediump float ambientDirectional = mix(1.0 - 0.3 * min(dirLuminance, 1.0), 1.0, min(ndl + 1.0, 1.0));
            // Environmental light blocked from below: a downward face keeps 92%, a roof all of it.
            mediump float vertical = mix(0.92, 1.0, normal.z * 0.5 + 0.5);
            // The sun is shadowed by the map AND by the back-face rule; the sky only by the map
            // (skyShadow), so a wall merely turned away from the sun keeps all of it. Without the
            // sky term a shadowed facade moved by 4% where the ground beside it went to a fifth -
            // mapbox's own ratio, whose ambient is four times its directional.
            //
            // Both are raised to 2.2 first, because this sum is LINEAR and the ground applies its
            // own shadow to a finished sRGB colour. A strength of 0.8 leaves a fifth of the light
            // there and pow(0.2, 1/2.2) = 0.48 here: the same setting, half the shadow. Squared
            // into the linear domain, the two come out at the same depth.
            mediump float linearSky = pow(skyShadow, 2.2);
            mediump float linearSun = pow(shadow, 2.2);
            mediump vec3 lit = u_ambientColor * (vertical * ambientDirectional * linearSky) + u_sunColor * (sunNdl * linearSun);
            // The light is summed in LINEAR space and only then returned to sRGB, which is the
            // whole reason their facades stay soft where a straight sRGB multiply crushes them.
            // Equivalent to linearTosRGB(sRGBToLinear(color) * lit), one pow instead of three.
            lit = pow(lit, vec3(1.0 / 2.2));
            // An EMITTED surface keeps its authored colour whatever the light does - mapbox's
            // `mix(apply_lighting(color), color, emissive_strength)`. At 0 this is a no-op, which is
            // what every extrusion in a converted Standard asks for; a lit window asks for more.
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
        // MapLibre's 'hillshade-exaggeration': the slope response curve and the overall strength
        // (HillshadeRasterTileLayer contrast). Default 0.5, matching the MapLibre style spec.
        uniform float u_intensity;

        #define PI 3.141592653589793
        #define STANDARD 0
        #define COMBINED 1
        #define IGOR 2
        #define MULTIDIRECTIONAL 3
        #define BASIC 4

        // All algorithms below composite in premultiplied alpha (the renderer blends normal map
        // tiles with GL_ONE, GL_ONE_MINUS_SRC_ALPHA), so the straight colors coming in from the
        // uniforms are premultiplied first - as MapLibre does before its shader ever runs.
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

        // Port of MapLibre's hillshade.fragment.glsl. Kept line-for-line comparable so the two
        // renderers can be diffed against each other; the only deliberate difference is that the
        // Mercator scale correction (MapLibre's 'scaleFactor') is baked into the normal map by
        // NormalMapBuilder instead of being recomputed per fragment from a latitude range.
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
        // weighted by the aspect. The user azimuth is unused by design - only the altitude matters.
        // Note MapLibre instead averages basic_hillshade over its illumination-source arrays, which
        // degenerates to plain BASIC for the single light source this layer exposes; GDAL's version
        // is used here so the mode is actually multidirectional.
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
            // Recover the height gradient from the perturbed normal. On a planar surface the
            // tangent frame flips x and y, so -normal.xy/normal.z gives (dh/dEast, dh/dNorth).
            // The y component is negated on top of that to match MapLibre, whose DEM texture has
            // north at v = 0 and therefore works with (dh/dEast, -dh/dNorth). Without it the
            // aspect is mirrored about the east-west axis and the light rotates the wrong way.
            vec2 deriv = vec2(-normal.x, normal.y) / max(normal.z, 0.001);

            // Extra vertical exaggeration, a Massif addition with no MapLibre equivalent. At the
            // default of 1.0 the slope is left exactly as the normal map encoded it.
            deriv *= u_exaggeration;

            // u_lightDir is (sin(compassAzimuth), cos(compassAzimuth), -sin(altitude)): the
            // horizontal part points towards the light, z points down towards the ground.
            // MapLibre adds PI to the compass azimuth for every method, because 0 degrees is north
            // and the original shader was written to accept (-illuminationDirection - 90).
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

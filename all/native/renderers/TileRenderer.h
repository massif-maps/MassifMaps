/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILERENDERER_H_
#define _MASSIF_TILERENDERER_H_

#include "graphics/Color.h"
#include "components/StyleEnvironment.h"
#include "graphics/ViewState.h"
#include "renderers/utils/GLResource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <map>
#include <tuple>
#include <vector>
#include <regex>
#include <optional>

#include <cglib/ray.h>

#include <vt/TileId.h>
#include <vt/Tile.h>
#include <vt/Bitmap.h>
#include <vt/Styles.h>
#include <vt/GLTileRenderer.h>

namespace massif {
    class ElevationTextureCache;
    class Options;
    class MapRenderer;
    class TerrainOptions;
    class TileDrawData;
    class ViewState;
    class VTRenderer;
    namespace vt {
        class LabelCuller;
        class TileTransformer;
    }
    
    class TileRenderer {
    public:
        TileRenderer();
        virtual ~TileRenderer();
    
        void setComponents(const std::weak_ptr<Options>& options, const std::weak_ptr<MapRenderer>& mapRenderer);

        std::shared_ptr<vt::TileTransformer> getTileTransformer() const;
        void setTileTransformer(const std::shared_ptr<vt::TileTransformer>& tileTransformer);
    
        void setInteractionMode(bool enabled);
        void setTerrainDepthWriteMode(bool enabled);
        void setTerrainRenderOrder(int order);
        void setLayerBlendingSpeed(float speed);
        void setLabelBlendingSpeed(float speed);
        void setLabelPerspectiveScaling(float scaling);
        void setLabelOrder(int order);
        void setBuildingOrder(int order);
        void setRasterFilterMode(vt::RasterFilterMode filterMode);
        void setNormalMapShadowColor(const Color& color);
        void setNormalMapHighlightColor(const Color& color);
        void setNormalMapAccentColor(const Color& color);
        void setNormalMapLightingShader(const std::string& shader);
        void setNormalMapElevationEncoded(bool enabled);
        void setNormalMapContourInterval(float interval);
        void setNormalMapContourColor(const Color& color);
        void setNormalMapContourWidth(float width);
        void setNormalIlluminationMapRotationEnabled(bool enabled);
        void setNormalIlluminationDirection(MapVec direction);
        void setHillshadeMethod(int method);
        void setHillshadeExaggeration(float exaggeration);
        void setHillshadeIntensity(float intensity);
        void setRendererLayerFilter(const std::optional<std::regex>& filter);
        void setClickHandlerLayerFilter(const std::optional<std::regex>& filter);

        void offsetLayerHorizontally(double offset);
    
        /**
         * Starts the vt frame without drawing, so the shared drape can be baked before any layer draws.
         * onDrawFrame calls it itself when it has not run this frame.
         */
        bool prepareFrame(float deltaSeconds, const ViewState& viewState);


        /**
         * Cross-layer drape support. The shared cache owns the textures; this renderer only
         * reports what it would drape and bakes its own content into a bound target.
         */
        void setExternalDrapeTarget(bool enabled);
        void setExternalDrapeTiles(const std::vector<vt::TileId>& tileIds);
        void setTerrainGroundTiles(const std::vector<vt::TileId>& tileIds, const std::vector<int>& proxyDepths);
        void setTerrainLayerOrdinalBase(int base);
        int getStyleLayerCount() const;
        /**
         * Per-tile drape bake resolution: the option's value, else derived from the screen. Static so
         * MapRenderer's drape cache resolves it exactly as every layer does.
         */
        static int resolveDrapeResolution(int setting, const ViewState& viewState, const std::shared_ptr<Options>& options, std::size_t budgetMegabytes = 0, int workingSet = 0);
        // Metres a draped line is drawn in front of the ground (see GLTileRenderer::setTerrainLineClearance).
        static float terrainLineClearanceMeters();
        // Style layers drawn live instead of draped (TerrainOptions::NoDrapeLayerFilter; demo builds:
        // debug.massif.nodrapelayers, "none" drapes all). See GLTileRenderer::setNoDrapeLayerFilter.
        static std::optional<std::regex> noDrapeLayerFilter(const std::string& optionFilter);
        static constexpr float DEFAULT_LINE_CLEARANCE_METERS = 25.0f;
        // The drape cache clamps to the same range (TerrainDrapeCache::setResolution).
        static constexpr int MIN_DRAPE_RESOLUTION = 128;
        static constexpr int MAX_DRAPE_RESOLUTION = 2048;
        // Tiles the automatic resolution assumes cached: the cover plus the stand-in generation, or zooms blink.
        static constexpr std::size_t DRAPE_WORKING_SET = 64;
        int renderTerrainGround(const Color& color);
        void collectDrapeTiles(std::map<vt::TileId, std::size_t>& drapeTiles) const;
        int bakeDrapeTile(const vt::TileId& tileId);
        void collectSpanDrapeTiles(std::map<vt::TileId, std::size_t>& spanTiles) const;
        void collectUnresolvedSpanEnds(std::vector<std::pair<int, cglib::vec2<double>>>& ends) const;
        int bakeSpanDrapeTile(const vt::TileId& tileId);
        void setSpanDrapeTextures(const std::map<vt::TileId, unsigned int>& textures);
        void setGroundDrapeTextures(const std::map<vt::TileId, vt::GLTileRenderer::GroundDrape>& drapes);
        // Drapeable style layers in draw order, flagged draped or live (GLTileRenderer::collectDrapeStackOrder).
        void collectDrapeStackOrder(std::vector<std::pair<int, bool> >& units) const;
        int bakeDrapeCoverage(const vt::TileId& tileId, int fromStyleLayerIdx);
        void setDrapeCoverageMasks(const std::vector<std::map<vt::TileId, unsigned int> >& maskTextures, const std::map<int, int>& styleLayerMasks);
        int renderDrapedSurface(const vt::TileId& tileId, unsigned int drapeTexture, float uvOffsetX, float uvOffsetY, float uvScale);
        int renderDrapedSurfaceFill(const vt::TileId& tileId, const Color& color);
        int blitDrapeTexture(unsigned int srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale);
        bool calculateShadowViewProj(const std::vector<vt::TileId>& tileIds, const std::vector<vt::TileId>& casterTileIds, const std::vector<std::pair<double, double> >& casterHeights, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<vt::TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const;
        float shadowCasterFadeSignature(const std::vector<vt::TileId>* coveredBy) const;
        int consumeShadowCastersMissingElevation();
        int renderShadowCasters(const std::vector<vt::TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround);
        void setTerrainShadowMap(unsigned int texture, int mapSize, int cascades, const cglib::vec3<float>& depthBias, const std::array<float, 4>& depthScales, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec2<float>& fadeRange, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, 4>& lightViewProjs);
        void setTerrainShadowMask(unsigned int texture, float invScreenWidth, float invScreenHeight);
        int renderTerrainShadowMask(const std::vector<vt::TileId>& tileIds);
        bool isGroundAOActive() const;
        bool isGroundAOBakeable() const;
        bool hasGroundContent() const;
        bool coversGround(const ViewState& viewState) const;
        int renderGroundAOMask();
        int bakeGroundAOMask(const vt::TileId& tileId);
        // Pushed before the shared terrain surface draws; onDrawFrame runs after it, a frame late.
        void setTerrainSunLighting(const ResolvedLighting& lighting);
        // Shared by setTerrainSunLighting and onDrawFrame so they cannot light one frame differently.
        static vt::GLTileRenderer::TerrainLighting buildTerrainLighting(const ResolvedLighting& lighting);
        // Linear-space light colour scaled by intensity, as the 3D lighting sums it.
        static cglib::vec3<float> linearColor(const Color& color, float intensity);
        // The terrain tiles a paint draws itself on when there is no drape to bake into.
        void setTerrainPaintTiles(const std::vector<vt::TileId>& tileIds);
        // Makes this renderer shade the elevation texture into the drape. The fingerprint must cover every
        // appearance parameter, or a baked drape survives a change.
        void setTerrainPaint(bool enabled, bool fullDetail, float heightScale, bool exaggerateHeightScale, bool legacyHeightScale, float contrast, float opacity, std::size_t fingerprint);

        bool onDrawFrame(float deltaSeconds, const ViewState& viewState);
        bool onDrawFrame3D(float deltaSeconds, const ViewState& viewState);
    
        /**
         * Places this layer's labels; `finished` is cleared when the culler's slice ran out first.
         * Returns false when there was nothing to place, so the pass owes no redraw.
         */
        bool cullLabels(vt::LabelCuller& culler, const ViewState& viewState, bool& finished);
        // Copy of the vt label occlusion test for the culler: an occluded label must not reserve a collision slot.
        void setLabelOcclusionTestCopy(std::function<bool(const cglib::vec3<double>&)> test);
        std::function<bool(const cglib::vec3<double>&)> getLabelOcclusionTest() const;
        void restartLabelPlacement();
        void snapLabelTransition();

        // spanReferenceTiles are never drawn, only read for bridge chords (TileLayer::collectSpanReferenceTiles).
        bool refreshTiles(const std::vector<std::shared_ptr<TileDrawData> >& drawDatas, const std::vector<std::shared_ptr<const vt::Tile> >& spanReferenceTiles = {});

        void calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, float radius, std::vector<vt::GLTileRenderer::GeometryIntersectionInfo>& results) const;
        void calculateRayIntersectedElements3D(const cglib::ray3<double>& ray, const ViewState& viewState, float radius, std::vector<vt::GLTileRenderer::GeometryIntersectionInfo>& results) const;
        void calculateRayIntersectedBitmaps(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<vt::GLTileRenderer::BitmapIntersectionInfo>& results) const;
    
        // This frame's style sun/shadow/fog; unset values fall back to LightOptions/TerrainOptions.
        void setStyleEnvironment(const StyleEnvironment& env);

        // brightness is view::brightness; pass it in, a ViewState built here defaults to full daylight.
        static Color evaluateColorFunc(const vt::ColorFunction& colorFunc, const ViewState& viewState, float brightness = 1.0f, float zoomShift = 0.0f);
        static float evaluateFloatFunc(const vt::FloatFunction& floatFunc, const ViewState& viewState, float brightness = 1.0f, float zoomShift = 0.0f);

        /**
         * True once, after the GL renderer was created with tiles already waiting: they missed label
         * placement, and a still camera never asks again. The owning layer requests a pass.
         */
        bool consumeLabelPlacementOwed();

    private:
        struct LabelOcclusionState;

        bool initializeRenderer();
        // The normal-map lighting shader and its uniforms, for initializeRenderer and an in-place swap.
        vt::GLTileRenderer::LightingShader createNormalMapLightingShader();
        bool isPlanarProjectionMode() const;
        // _mutex taken from the render thread, timed: a tile-set change holds it on the cull thread.
        std::unique_lock<std::mutex> lockTimed() const;
        // debug.massif.depthshift, read once. Android demo builds only.
        static float getTerrainContentDepthShift();
        // tangram res/scenes/terrain-3d.yaml: depth_shift = -0.02*u_proj[2][3], and [2][3] is -1.
        static constexpr float TERRAIN_TANGRAM_DEPTH_SHIFT = 0.02f;
        // Elevation levels shading resolves beyond ElevationManager::clampTileZoom; 0 shares geometry's (tangram).
        static constexpr int DEFAULT_PAINT_DETAIL_LEVELS = 0;
        static int terrainPaintDetailLevels();
        // debug.massif.groundpaint 1 draws the paint as the ground (tangram). Android demo builds only.
        static bool isTerrainPaintOnGroundForced();
        // Texture fetches per terrain vertex, debug.massif.demtaps. Android demo builds only.
        static int terrainDemTaps();
        // debug.massif.tilebg 1 keeps the per-tile per-layer background meshes. Android demo builds only.
        static bool isTerrainTileBackgroundsForced();
        // debug.massif.tilemasks forces the stencil tile masks on (1) or off (0). Android demo builds only.
        static int tileMasksMode();
        // debug.massif.inline3d 0 draws extrusions through the per-layer 3D overlay. Android demo builds only.
        static bool isInline3DEnabled();
        // pass 0 = the layer's own, 1 = the last. Billboards must follow the extrusions or buildings cover them.
        bool drawsBillboardLabelsHere(int pass) const { return std::max(_labelOrder, _buildingOrder) == pass; }
        void updateLabelOcclusionTest(const std::shared_ptr<vt::GLTileRenderer>& tileRenderer, const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions);

        static constexpr int SURFACE_RESET_DELAY = 500; // minimum interval (ms) between elevation-driven tile surface rebuilds

        static constexpr float MIN_OCCLUSION_TOLERANCE = 0.01f; // relative depth slack a label anchored on the terrain always gets

        static const std::string LIGHTING_SHADER_2D;
        static const std::string LIGHTING_SHADER_3D;
        static const std::string LIGHTING_SHADER_NORMALMAP;

        // MapLibre's light defaults (mbgl light_impl.hpp): spherical (1.15, 210, 30), intensity 0.5, vertical gradient on.
        static const cglib::vec3<float> ML_LIGHT_POS;
        static constexpr float ML_LIGHT_INTENSITY = 0.5f;
        static constexpr float ML_VERTICAL_GRADIENT = 1.0f;

        std::weak_ptr<MapRenderer> _mapRenderer;
        std::weak_ptr<Options> _options;
        StyleEnvironment _styleEnvironment;
        std::shared_ptr<vt::TileTransformer> _tileTransformer;

        std::shared_ptr<VTRenderer> _vtRenderer;
        bool _labelPlacementOwed = false; // see consumeLabelPlacementOwed
        unsigned int _labelOcclusionDepthVersion = 0; // the terrain occlusion depth the labels were last placed against
        bool _interactionMode;
        float _layerBlendingSpeed;
        float _labelBlendingSpeed;
        float _labelPerspectiveScaling;
        int _labelOrder;
        int _buildingOrder;
        vt::RasterFilterMode _rasterFilterMode;
        Color _normalMapShadowColor;
        Color _normalMapAccentColor;
        Color _normalMapHighlightColor;
        std::string _normalMapLightingShader;
        bool _normalMapElevationEncoded = false;
        float _normalMapContourInterval = 0.0f; // meters; <= 0 disables contour lines
        Color _normalMapContourColor;
        float _normalMapContourWidth = 0.75f; // contour half-width in screen pixels
        std::optional<std::regex> _rendererLayerFilter;
        std::weak_ptr<vt::GLTileRenderer> _rendererLayerFilterTarget;
        std::optional<std::regex> _clickHandlerLayerFilter;

        double _horizontalLayerOffset;
        cglib::vec3<float> _viewDir;
        // Resolved sun (style over LightOptions), captured for the draw-time 3D lighting callback.
        cglib::vec3<float> _resolvedSunDir = cglib::vec3<float>(0, 0, 1);
        // Altitude-floored, for the extrusions.
        cglib::vec3<float> _resolvedBuildingSunDir = cglib::vec3<float>(0, 0, 1);
        Color _resolvedSunColor = Color(255, 255, 255, 255);
        Color _resolvedAmbientColor = Color(255, 255, 255, 255);
        // Scene light on a flat upward surface, linear space (resolveLighting).
        cglib::vec3<float> _resolvedRadiance = cglib::vec3<float>(1.0f, 1.0f, 1.0f);
        float _buildingEmissive = 0.0f;
        float _backgroundEmissive = 1.0f;
        // mapbox's measure-light brightness, what a style reads as view::brightness.
        float _resolvedBrightness = 1.0f;
        // Separate from the global version, which an exaggeration-only change also bumps.
        unsigned int _elevationDataVersion = 0;
        // ...and the exaggeration itself, which every CPU height carries. -1 = never read.
        float _elevationExaggeration = -1.0f;
        // StyleEnvironment::resolveLighting.
        float _buildingLightIntensity = 1.0f;
        float _buildingAmbient = 0.35f;
        float _buildingVerticalGradient = 0.65f;
        float _buildingRoofShade = 1.0f;
        // Light the walls maplibre's way rather than mapbox's - set for a style that lights nothing.
        bool _buildingLightingMapLibre = false;
        float _buildingHeightScale = 1.0f;
        float _buildingHeightViewScale = 1.0f;
        bool _buildingGrowOnAppear = false;
        bool _buildingFadeOnAppear = true;
        std::atomic<float> _textOcclusionOpacity{1.0f};
        std::atomic<float> _styleZoomShift{0.0f};
        float _groundAOIntensity = 0.5f;
        float _groundAOAttenuation = 0.69f;
        cglib::vec3<float> _normalLightDir;
        MapVec _normalIlluminationDirection;
        bool _normalIlluminationMapRotationEnabled;
        double _mapRotation;
        int _hillshadeMethod;
        float _hillshadeExaggeration;
        float _hillshadeIntensity;
        bool _terrainDepthWriteMode = false;
        bool _essl3FallbackReported = false;  // the ESSL 3.00 -> 1.00 fallback warning is logged once
        bool _terrainPaintEnabled = false; // this renderer shades the DEM instead of drawing tiles
        bool _terrainPaintFullDetail = true; // shade from the DEM's own max zoom, not the mesh's level
        bool prepareFrameUnsafe(float deltaSeconds, const ViewState& viewState); // caller holds _mutex
        void pushTerrainDrapeState(); // caller holds _mutex

        bool _framePrepared = false;   // prepareFrame already ran this frame
        bool _framePrepareResult = false;
        bool _externalDrapeTarget = false;
        bool _terrainGroundActive = false; // a shared ground cover is set: this stack draws a terrain surface without a drape
        int _terrainRenderOrder = 0;
        int _maxVertexTextureUnits = -1; // lazily queried GL capability (-1 = not queried yet)
        std::shared_ptr<ElevationTextureCache> _elevationTextureCache;
        unsigned int _elevationVersion = 0;
        // Pushing the extrusion provider invalidates every base: only when this pair changes.
        std::pair<const void*, const void*> _extrusionProviderKey { nullptr, nullptr };
        std::optional<std::chrono::steady_clock::time_point> _lastSurfaceResetTime;
        std::shared_ptr<LabelOcclusionState> _labelOcclusionState;
        mutable std::mutex _labelOcclusionTestMutex;
        std::function<bool(const cglib::vec3<double>&)> _labelOcclusionTestCopy;

        std::vector<vt::TileId> _terrainPaintTileIds; // last pushed, so an unchanged cover costs no vt lock
        std::map<vt::TileId, std::shared_ptr<const vt::Tile> > _tiles;
        // Offscreen tiles: their labels are placed, their geometry is never drawn. See refreshTiles.
        std::map<vt::TileId, std::shared_ptr<const vt::Tile> > _labelOnlyTiles;
        // Past the view on the sun's side: only their extrusions' shadows are drawn.
        std::map<vt::TileId, std::shared_ptr<const vt::Tile> > _shadowCasterTiles;
        std::vector<std::shared_ptr<const vt::Tile> > _spanReferenceTiles;
        
        mutable std::mutex _mutex;
    };
    
}

#endif

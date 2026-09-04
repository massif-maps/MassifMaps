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
         * Starts the vt frame (tile set, blending, compiled resources) without drawing anything.
         * Cross-layer draping needs every participating layer's render tiles ready BEFORE any of
         * them draws, so the shared drape can be baked first. onDrawFrame calls this itself when
         * it has not already run for this frame.
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
         * The per-tile drape texture resolution to bake at: the option's value when it sets one,
         * otherwise taken from the screen (see the implementation). Static so the drape CACHE,
         * which is owned by MapRenderer and must agree with every layer's renderer, resolves it
         * the same way.
         */
        static int resolveDrapeResolution(int setting, const ViewState& viewState, const std::shared_ptr<Options>& options);
        // Metres a draped line is drawn in front of the ground (see GLTileRenderer::setTerrainLineClearance).
        static float terrainLineClearanceMeters();
        // Style layers kept out of the drape bake and drawn live instead: the application's
        // TerrainOptions::NoDrapeLayerFilter, overridden by debug.massif.nodrapelayers ("none" to
        // drape everything). Compiled once per distinct pattern. See
        // GLTileRenderer::setNoDrapeLayerFilter.
        static std::optional<std::regex> noDrapeLayerFilter(const std::string& optionFilter);
        static constexpr float DEFAULT_LINE_CLEARANCE_METERS = 25.0f;
        // The drape cache clamps to the same range (TerrainDrapeCache::setResolution).
        static constexpr int MIN_DRAPE_RESOLUTION = 128;
        static constexpr int MAX_DRAPE_RESOLUTION = 2048;
        // Tiles the automatic resolution assumes are cached at once: the live cover plus what a pan
        // is about to need back. The resolution is lowered until that many fit the cache budget.
        // 24, not 64: at 64 the arithmetic pins the automatic resolution to 512 on EVERY device
        // (64 x 1024^2 x 4 = 256 MB against a 96 MB budget), which is half mapbox's linear
        // resolution for the same tile - they bake at 1024 - and that is the drape sharpness gap.
        // A real cover was measured at 15-34 leaves, so 64 was buying headroom nothing used.
        static constexpr std::size_t DRAPE_WORKING_SET = 24;
        int renderTerrainGround(const Color& color);
        void collectDrapeTiles(std::map<vt::TileId, std::size_t>& drapeTiles) const;
        int bakeDrapeTile(const vt::TileId& tileId);
        void collectSpanDrapeTiles(std::map<vt::TileId, std::size_t>& spanTiles) const;
        void collectUnresolvedSpanEnds(std::vector<std::pair<int, cglib::vec2<double>>>& ends) const;
        int bakeSpanDrapeTile(const vt::TileId& tileId);
        void setSpanDrapeTextures(const std::map<vt::TileId, unsigned int>& textures);
        // This layer's style layers with drapeable content, in draw order, each flagged draped or
        // live. The owner concatenates them across layers to place a live layer in the whole stack
        // (see GLTileRenderer::collectDrapeStackOrder).
        void collectDrapeStackOrder(std::vector<std::pair<int, bool> >& units) const;
        int bakeDrapeCoverage(const vt::TileId& tileId, int fromStyleLayerIdx);
        void setDrapeCoverageMasks(const std::vector<std::map<vt::TileId, unsigned int> >& maskTextures, const std::map<int, int>& styleLayerMasks);
        int renderDrapedSurface(const vt::TileId& tileId, unsigned int drapeTexture, float uvOffsetX, float uvOffsetY, float uvScale);
        int renderDrapedSurfaceFill(const vt::TileId& tileId, const Color& color);
        int blitDrapeTexture(unsigned int srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale);
        bool calculateShadowViewProj(const std::vector<vt::TileId>& tileIds, const std::vector<vt::TileId>& casterTileIds, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<vt::TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const;
        float shadowCasterFadeSignature(const std::vector<vt::TileId>* coveredBy) const;
        int consumeShadowCastersMissingElevation();
        int renderShadowCasters(const std::vector<vt::TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround);
        void setTerrainShadowMap(unsigned int texture, int mapSize, int cascades, const cglib::vec3<float>& depthBias, const std::array<float, 4>& depthScales, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec2<float>& fadeRange, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, 4>& lightViewProjs);
        void setTerrainShadowMask(unsigned int texture, float invScreenWidth, float invScreenHeight);
        int renderTerrainShadowMask(const std::vector<vt::TileId>& tileIds);
        bool isGroundAOActive() const;
        bool isGroundAOBakeable() const;
        void setLabelOcclusionDepth(unsigned int depthTexture, float occluderSize);
        // Whether anything wants labels occluded by 3D content: the resolved TerrainOptions/Map
        // default, or a style layer with its own text-occlusion-opacity.
        bool isLabelOcclusionWanted() const;
        int renderLabelOcclusionDepth();
        int renderGroundAOMask();
        int bakeGroundAOMask(const vt::TileId& tileId);
        // Pushed by the owner BEFORE the shared terrain surface is drawn. onDrawFrame sets the same
        // state, but it runs after that draw, so the surface would light itself with the PREVIOUS
        // frame's sun - invisible while the map redrew continuously, and a change that appears not
        // to apply at all once it goes idle.
        void setTerrainSunLighting(const ResolvedLighting& lighting);
        // The vt-side lighting struct for a resolved sun. One place, so the pre-surface push above
        // and onDrawFrame cannot light the same frame differently.
        static vt::GLTileRenderer::TerrainLighting buildTerrainLighting(const ResolvedLighting& lighting);
        // A light colour in LINEAR space, scaled by its intensity - the form the 3D lighting sums in.
        static cglib::vec3<float> linearColor(const Color& color, float intensity);
        // Turns this renderer into a terrain paint baker: it shades the shared terrain elevation
        // texture into the drape texture, at its own place in the layer order, instead of holding
        // a tile set of its own. The fingerprint must cover every value the paint's appearance
        // depends on, including the lighting shader's own uniforms, or already-baked drape
        // textures survive a parameter change.
        // The terrain tiles a paint draws itself on when there is no drape to bake into.
        void setTerrainPaintTiles(const std::vector<vt::TileId>& tileIds);
        void setTerrainPaint(bool enabled, bool fullDetail, float heightScale, bool exaggerateHeightScale, bool legacyHeightScale, float contrast, float opacity, std::size_t fingerprint);

        bool onDrawFrame(float deltaSeconds, const ViewState& viewState);
        bool onDrawFrame3D(float deltaSeconds, const ViewState& viewState);
    
        bool cullLabels(vt::LabelCuller& culler, const ViewState& viewState);

        // `spanReferenceTiles`: fetched unseen for a stranded bridge's chord, unioned by the
        // renderer and never drawn - see TileLayer::collectSpanReferenceTiles.
        bool refreshTiles(const std::vector<std::shared_ptr<TileDrawData> >& drawDatas, const std::vector<std::shared_ptr<const vt::Tile> >& spanReferenceTiles = {});

        void calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, float radius, std::vector<vt::GLTileRenderer::GeometryIntersectionInfo>& results) const;
        void calculateRayIntersectedElements3D(const cglib::ray3<double>& ray, const ViewState& viewState, float radius, std::vector<vt::GLTileRenderer::GeometryIntersectionInfo>& results) const;
        void calculateRayIntersectedBitmaps(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<vt::GLTileRenderer::BitmapIntersectionInfo>& results) const;
    
        // The style's own sun/shadow/fog values for this frame, pushed by the layer that owns
        // this renderer. What the style leaves unset comes from LightOptions/TerrainOptions.
        void setStyleEnvironment(const StyleEnvironment& env);

        static Color evaluateColorFunc(const vt::ColorFunction& colorFunc, const ViewState& viewState);
        static float evaluateFloatFunc(const vt::FloatFunction& floatFunc, const ViewState& viewState);

    private:
        struct LabelOcclusionState;

        bool initializeRenderer();
        bool isPlanarProjectionMode() const;
        // Tangram-model measurement switch, read once from debug.massif.depthshift (Android only).
        static float getTerrainContentDepthShift();
        // tangram res/scenes/terrain-3d.yaml: depth_shift = -0.02*u_proj[2][3], and [2][3] is -1.
        static constexpr float TERRAIN_TANGRAM_DEPTH_SHIFT = 0.02f;
        // It is a per-step separation between coplanar style layers, not a budget to spread over
        // the stack: scaling it by the ordinal span was this fork's, and ten times their pull is
        // what let far content over a near ridge (see the shift's use in onDrawFrame).
        // Elevation levels the shading texture resolves BEYOND the standard rule
        // (ElevationManager::clampTileZoom, which is tangram's: the tile's own zoom, adjusted by
        // the elevation source's zoom bias). 0 means the shading and the geometry read the SAME
        // elevation tile, which is tangram's arrangement - one raster per tile serves both - and
        // is also why it costs nothing: there is no second set of grids and textures.
        static constexpr int DEFAULT_PAINT_DETAIL_LEVELS = 0;
        static int terrainPaintDetailLevels();
        // Measurement switch for tangram's arrangement: the paint drawn AS the ground rather than
        // as its layer's own surface over it. debug.massif.groundpaint 1. Read once (Android only).
        static bool isTerrainPaintOnGroundForced();
        // Texture fetches per terrain vertex, debug.massif.demtaps. Read once (Android only).
        static int terrainDemTaps();
        // debug.massif.tilebg 1 keeps the per-tile per-layer background meshes. Read once (Android).
        static bool isTerrainTileBackgroundsForced();
        // debug.massif.tilemasks forces the stencil tile masks on (1) or off (0) instead of the
        // renderer's own rule. Read once (Android only).
        static int tileMasksMode();
        // debug.massif.inline3d 0 sends the 3D extrusions back through the per-layer 3D overlay
        // instead of drawing them inline in the main framebuffer. Read once (Android only).
        static bool isInline3DEnabled();
        // Is `pass` (0 = the layer's own, 1 = the last one) where the BILLBOARD labels belong?
        // A billboard stands out of the map and has to follow the extrusions, which default to the
        // last pass while the labels default to their layer's - drawn at the label order alone,
        // every billboard of a layer was painted over by that layer's own buildings.
        // Flat labels keep the label order: they lie on the ground and a building over one is right.
        bool drawsBillboardLabelsHere(int pass) const { return std::max(_labelOrder, _buildingOrder) == pass; }
        void updateLabelOcclusionTest(const std::shared_ptr<vt::GLTileRenderer>& tileRenderer, const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions);

        static constexpr int SURFACE_RESET_DELAY = 500; // minimum interval (ms) between elevation-driven tile surface rebuilds

        static constexpr float MIN_OCCLUSION_TOLERANCE = 0.01f; // relative depth slack a label anchored on the terrain always gets

        static const std::string LIGHTING_SHADER_2D;
        static const std::string LIGHTING_SHADER_3D;
        static const std::string LIGHTING_SHADER_NORMALMAP;

        std::weak_ptr<MapRenderer> _mapRenderer;
        std::weak_ptr<Options> _options;
        StyleEnvironment _styleEnvironment;
        std::shared_ptr<vt::TileTransformer> _tileTransformer;

        std::shared_ptr<VTRenderer> _vtRenderer;
        bool _interactionMode;
        float _layerBlendingSpeed;
        float _labelBlendingSpeed;
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
        std::optional<std::regex> _clickHandlerLayerFilter;

        double _horizontalLayerOffset;
        cglib::vec3<float> _viewDir;
        // The sun as RESOLVED (style over LightOptions), captured each frame for the 3D lighting
        // shader callback, which runs at draw time and cannot resolve it itself.
        cglib::vec3<float> _resolvedSunDir = cglib::vec3<float>(0, 0, 1);
        // The same sun with its altitude floored, which is what the extrusions light with.
        cglib::vec3<float> _resolvedBuildingSunDir = cglib::vec3<float>(0, 0, 1);
        Color _resolvedSunColor = Color(255, 255, 255, 255);
        Color _resolvedAmbientColor = Color(255, 255, 255, 255);
        // The scene light on a flat, upward-facing surface, in linear space - see resolveLighting.
        cglib::vec3<float> _resolvedRadiance = cglib::vec3<float>(1.0f, 1.0f, 1.0f);
        float _buildingEmissive = 0.0f;
        float _backgroundEmissive = 1.0f;
        // mapbox's measure-light brightness, what a style reads as view::brightness.
        float _resolvedBrightness = 1.0f;
        // The elevation DATA version last acted on, apart from the global one: a change to only
        // the exaggeration moves the global version without making any surface stale.
        unsigned int _elevationDataVersion = 0;
        // What the extrusions light with, resolved from the style over the options
        // (StyleEnvironment::resolveLighting).
        float _buildingLightIntensity = 1.0f;
        float _buildingAmbient = 0.35f;
        float _buildingVerticalGradient = 0.65f;
        float _buildingRoofShade = 1.0f;
        // The style's extrusion height multiplier, and whether a tile's fade-in raises its
        // buildings with it (off: no source style asks for that animation).
        float _buildingHeightScale = 1.0f;
        float _buildingHeightViewScale = 1.0f;
        bool _buildingGrowOnAppear = false;
        bool _buildingFadeOnAppear = true;
        std::atomic<float> _textOcclusionOpacity{1.0f};
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

        bool _framePrepared = false;   // startFrame already ran this frame (cross-layer drape ordering)
        bool _framePrepareResult = false;
        bool _externalDrapeTarget = false;
        bool _terrainGroundActive = false; // a shared ground cover is set: this stack draws a terrain surface without a drape
        int _terrainRenderOrder = 0;
        int _maxVertexTextureUnits = -1; // lazily queried GL capability (-1 = not queried yet)
        std::shared_ptr<ElevationTextureCache> _elevationTextureCache;
        unsigned int _elevationVersion = 0;
        std::optional<std::chrono::steady_clock::time_point> _lastSurfaceResetTime;
        std::shared_ptr<LabelOcclusionState> _labelOcclusionState;

        std::map<vt::TileId, std::shared_ptr<const vt::Tile> > _tiles;
        std::vector<std::shared_ptr<const vt::Tile> > _spanReferenceTiles;
        
        mutable std::mutex _mutex;
    };
    
}

#endif

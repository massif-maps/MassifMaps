/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPRENDERER_H_
#define _MASSIF_MAPRENDERER_H_

#include "core/MapPos.h"
#include "core/MapVec.h"
#include "core/ScreenPos.h"
#include "core/ScreenBounds.h"
#include "components/DirectorPtr.h"
#include "graphics/ViewState.h"
#include "renderers/BackgroundRenderer.h"
#include "renderers/SkyRenderer.h"
#include "renderers/components/AnimationHandler.h"
#include "renderers/components/KineticEventHandler.h"
#include "components/StyleEnvironment.h"
#include "terrain/AutoFlatten.h"
#include "terrain/FlattenSwitch.h"
#include "ui/MapMoveReason.h"

#include <cglib/mat.h>
#include <cglib/ray.h>
#include <vt/TileId.h>

#include <array>
#include <atomic>
#include <optional>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include <map>

namespace massif {
    class CameraPanEvent;
    class CameraRotationEvent;
    class CameraTiltEvent;
    class CameraZoomEvent;
    class Bitmap;
    class BillboardDrawData;
    class ElevationManager;
    class Layer;
    class Layers;
    class MapRendererListener;
    class RendererCaptureListener;
    class RedrawRequestListener;
    class RayIntersectedElement;
    class Options;
    class PostProcessEffect;
    class TerrainRenderer;
    class TerrainOptions;
    class TileLayer;
    class TerrainDrapeCache;
    class TerrainShadowMap;
    class ScreenMaskBuffer;
    class ThreadWorker;
    class CullWorker;
    class VTLabelPlacementWorker;
    class BillboardPlacementWorker;
    class FrameBuffer;
    class Shader;
    class Texture;
    class GLResourceManager;

    /**
     * The map renderer component.
     */
    class MapRenderer : public std::enable_shared_from_this<MapRenderer> {
    public:
        struct OnChangeListener {
            virtual ~OnChangeListener() { }
            
            virtual void onMapChanged(MapMoveReason::MapMoveReason reason) = 0;
            virtual void onMapIdle() = 0;
        };

        MapRenderer(const std::shared_ptr<Layers>& layers, const std::shared_ptr<Options>& options);
        virtual ~MapRenderer();

        void init();
        void deinit();

        std::shared_ptr<RedrawRequestListener> getRedrawRequestListener() const;
        void setRedrawRequestListener(const std::shared_ptr<RedrawRequestListener>& listener);

        /**
         * Returns the map renderer listener. Can be null.
         * @return The map renderer listener.
         */
        std::shared_ptr<MapRendererListener> getMapRendererListener() const;
        /**
         * Sets the map renderer listener.
         * @param listener The new map renderer listener. Can be null.
         */
        void setMapRendererListener(const std::shared_ptr<MapRendererListener>& listener);
        
        /**
         * Returns the current view state.
         * @return The current view state.
         */
        ViewState getViewState() const;

        /**
         * Returns the current projectin surface object.
         * @return The current projection surface object.
         */
        std::shared_ptr<ProjectionSurface> getProjectionSurface() const;
    
        /**
         * Requests the renderer to refresh the view.
         * Note that there is normally no need to do this manually,
         * SDK automatically redraws the view when needed.
         * The default arguments record the CALL SITE, so a view that never stops redrawing can
         * say which of the ~30 callers is driving it (logRedrawSources below). Callers pass
         * nothing; the compiler fills these in.
         */
#if defined(__clang__) || defined(__GNUC__)
        void requestRedraw(const char* callerFile = __builtin_FILE(), int callerLine = __builtin_LINE()) const;
#else
        void requestRedraw(const char* callerFile = "?", int callerLine = 0) const;
#endif
    
        /**
         * Captures map rendering as a bitmap. This operation is asynchronous and the result is returned via listener callback.
         * @param listener The listener interface that will receive the callback once rendering is available.
         * @param waitWhileUpdating If true, delay the capture until all asynchronous processes are finished (for example, until all tiles are loaded).
         */
        void captureRendering(const std::shared_ptr<RendererCaptureListener>& listener, bool waitWhileUpdating);

        /**
         * Returns the current post-process effect. Can be null.
         * @return The current post-process effect.
         */
        std::shared_ptr<PostProcessEffect> getPostProcessEffect() const;
        /**
         * Sets the post-process effect. When set, the map is rendered into an offscreen
         * buffer and the effect fragment shader produces the final screen output.
         * Note: this feature is experimental and may change in future SDK versions.
         * @param postProcessEffect The new post-process effect. Can be null.
         */
        void setPostProcessEffect(const std::shared_ptr<PostProcessEffect>& postProcessEffect);

        std::shared_ptr<Layers> getLayers() const;

        std::shared_ptr<Options> getOptions() const;
        
        std::shared_ptr<GLResourceManager> getGLResourceManager() const;

        /**
         * Returns the terrain renderer (may be null). GL thread only. Internal method.
         */
        TerrainRenderer* getTerrainRenderer() const { return _terrainRenderer.get(); }

        /**
         * This frame's fog, resolved once from the options and the merged style opinion before
         * anything draws. The vector element renderers read it from here rather than taking it
         * through every onDrawFrame signature. GL thread only. Internal method.
         */
        const ResolvedFog& getFrameFog() const { return _frameFog; }

        std::vector<std::shared_ptr<BillboardDrawData> > getBillboardDrawDatas() const;
    
        AnimationHandler& getAnimationHandler();
        KineticEventHandler& getKineticEventHandler();

        // reason travels with the event so the camera listeners can say what moved the map. An
        // animated call reports it once, here; the frames it produces report ANIMATION.
        void calculateCameraEvent(CameraPanEvent& cameraEvent, float durationSeconds, bool updateKinetic, MapMoveReason::MapMoveReason reason);
        void calculateCameraEvent(CameraRotationEvent& cameraEvent, float durationSeconds, bool updateKinetic, MapMoveReason::MapMoveReason reason);
        void calculateCameraEvent(CameraTiltEvent& cameraEvent, float durationSeconds, bool updateKinetic, MapMoveReason::MapMoveReason reason);
        void calculateCameraEvent(CameraZoomEvent& cameraEvent, float durationSeconds, bool updateKinetic, MapMoveReason::MapMoveReason reason);
    
        void moveToFitBounds(const MapBounds& mapBounds, const ScreenBounds& screenBounds, bool integerZoom, bool resetTilt, bool resetRotation, float durationSeconds);
        
        void onSurfaceCreated();
        void onSurfaceChanged(int width, int height);
        void onDrawFrame();
        void onSurfaceDestroyed();

        void finishRendering();

        void clearAndBindScreenFBO(const Color& color, bool depth, bool stencil);
        void blendAndUnbindScreenFBO(float opacity);
        // Draws a full-screen quad sampling the mask. Sets NO render state - the caller owns blend,
        // depth and culling, because one caller runs inside the drape bake, which has its own.
        void drawMaskQuad(unsigned int texture, float invWidth, float invHeight);
        // The same, wrapped in the state for a plain screen multiply (dst *= mask).
        void multiplyScreenMask(unsigned int texture, float invWidth, float invHeight);
        void setZBuffering(bool enable);
    
        void calculateRayIntersectedElements(const MapPos& targetPos, ViewState& viewState, std::vector<RayIntersectedElement>& results);
        // Same, for a ray that never meets the ground - a touch aimed at the sky. Layers whose
        // content is anchored in the sky (CelestialLayer) are only reachable this way.
        void calculateRayIntersectedElements(const cglib::ray3<double>& ray, ViewState& viewState, std::vector<RayIntersectedElement>& results);
    
        void billboardsChanged();
        void vtLabelsChanged(const std::shared_ptr<Layer>& layer, bool delay);
        void layerChanged(const std::shared_ptr<Layer>& layer, bool delay);
        void viewChanged(bool delay, MapMoveReason::MapMoveReason reason);
    
        void registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        void unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        
    private:
        // debug.massif.background 0 drops the map background plane. Read once (Android only).
        static bool isBackgroundEnabled();
        class OptionsListener : public Options::OnChangeListener {
        public:
            explicit OptionsListener(const std::shared_ptr<MapRenderer>& mapRenderer);
            
            virtual void onOptionChanged(const std::string& optionName);
            
        private:
            std::weak_ptr<MapRenderer> _mapRenderer;
        };

        void initializeRenderState() const;

        // Dumps and resets the per-call-site redraw request counts. Diagnostic for "the map never
        // stops rendering": the counts say whether the frames come from an animation, from tiles
        // arriving, or from one caller firing on every single frame.
        static void logRedrawSources();

        // postProcessing tells whether an effect is going to run this frame: only then are the
        // layers that opted out of it held back for drawOverlayLayers.
        // Every tile layer's Map-block opinion, merged - the first layer to define a property
        // wins. Collected ONCE per frame, before the sky draws, because the sky, the background
        // plane, the terrain surface and the tile content must all fog the same way and the sky
        // is drawn long before drawLayers would have gathered it.
        StyleEnvironment collectStyleEnvironment(const ViewState& viewState) const;

        void drawLayers(float deltaSeconds, const ViewState& viewState, bool postProcessing);

        // The layers drawLayers held back because they opted out of post-processing, drawn once
        // the effect has resolved into the framebuffer's secondary color texture - same depth
        // buffer, so they are still occluded by the terrain.
        void drawOverlayLayers(float deltaSeconds, const ViewState& viewState);

        // Is tileId a STRICT ancestor of other, i.e. does it cover its ground at a coarser level?
        static bool coversTile(const vt::TileId& tileId, const vt::TileId& other);

        // The terrain cover the whole tile layer stack shares this frame: the union of what the
        // layers report (layerTiles / collectedTiles, kept for the drape's staleness bookkeeping)
        // normalised into ONE non-overlapping quadtree partition, the leaves. Both terrain paths
        // build on it - the drape bakes one texture per leaf, the shared ground draws one surface
        // per leaf - because the surfaces of two different tesselations of the same height field
        // do not agree and fight wherever they overlap.
        // `extendSeedsOnly` keeps the seed to the levels the layers do NOT reach, which is what the
        // drape wants: one leaf there is one cache texture and one bake, so it pays for the extra
        // depth past a source's maxzoom and for nothing else. The shared ground takes the seed
        // whole (false) - it has no texture budget and needs the full view covered.
        void collectTerrainCover(const std::vector<std::shared_ptr<TileLayer> >& tileLayers, const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::vector<vt::TileId>& seedTileIds, bool extendSeedsOnly, std::vector<std::map<vt::TileId, std::size_t> >& layerTiles, std::map<vt::TileId, std::size_t>& collectedTiles, std::vector<vt::TileId>& leaves, int& coverZoom, int& maxCollectedZoom);

        // The terrain's own camera-driven cover, the seed both paths above are built from. It is
        // what the camera can see rather than what the layers fetched, so it reaches
        // floor(camera zoom) whatever zoom a data source stops at.
        std::vector<vt::TileId> collectTerrainCoverTileIds(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions) const;

        // Directional shadows for one terrain stack: resolves the light from the styles, fits a
        // light box per cascade to the cover, re-renders the caster pass only when it has actually
        // changed, and hands the map (or none) and the sun to every layer. The cover is the only
        // difference between the drape path and the shared-ground one, so both call this.
        // contentChanged says whether the tile content moved this frame - it rations the
        // content-driven refreshes, which camera-driven ones are not subject to.
        void applyTerrainShadows(const std::vector<std::shared_ptr<TileLayer> >& tileLayers, const std::vector<vt::TileId>& coverTileIds, const std::shared_ptr<TerrainOptions>& terrainOptions, const ViewState& viewState, int prevFBO, bool contentChanged, bool castShadows, ResolvedLighting& lighting, std::array<double, 4>& shadowTexelMeters);

        // keepBound resolves the effect into the screen framebuffer's secondary color texture and
        // leaves it bound (for overlay layers), instead of writing straight to the screen.
        void applyPostProcessEffect(const std::shared_ptr<PostProcessEffect>& effect, const ViewState& viewState, bool keepBound = false);

        void handleRendererCaptureCallbacks();

        // How far the highest ground in view moves on screen because it is displaced, in pixels.
        double calculateTerrainParallax(const std::shared_ptr<TerrainOptions>& terrainOptions) const;
        // Runs the auto-flatten rule and steps the 2D/3D switch. Returns true when the terrain
        // DECODE state changed this frame, which is the only moment the visible tile set has to be
        // recomputed.
        bool updateTerrainFlatten(float deltaSeconds);

        static const int BILLBOARD_PLACEMENT_TASK_DELAY;
        static const int VT_LABEL_PLACEMENT_TASK_DELAY;
        // Zoom change that asks for a label placement pass of its own, and how long after the last
        // one the pass runs (see viewChanged).
        static const float LABEL_PLACEMENT_ZOOM_THRESHOLD;
        static const int LABEL_PLACEMENT_ZOOM_DELAY;

        static const int ELEVATION_REFRESH_DELAY; // milliseconds between vector layer refreshes caused by elevation data changes
        static const float TERRAIN_SWITCH_WARM_TIMEOUT; // seconds the 2D/3D switch waits for the tiles 3D needs

        static const std::string BLEND_VERTEX_SHADER;
        static const std::string BLEND_FRAGMENT_SHADER;
        static const std::string POST_PROCESS_VERTEX_SHADER;
        
        std::optional<std::chrono::steady_clock::time_point> _lastFrameTime;
    
        ViewState _viewState;
        float _lastLabelPlacementZoom = 0.0f;

        // The 2D/3D switch. The ratio and the decode state live on TerrainOptions, where everything
        // reads them; what is kept here is the phase the switch is in.
        FlattenSwitch::State _flattenSwitchState;
        AutoFlatten::Trigger _autoFlattenTrigger;
        // Auto-flattening reads its parallax from the elevation height range, and that range is
        // only meaningful once the DEM has stopped arriving: a partly loaded view reports a small
        // range, which reads as small parallax and flattens the map - and flattening stops the
        // elevation decode, so it never recovers. These watch the data version and hold the rule
        // off until it has been still for TERRAIN_SWITCH_WARM_TIMEOUT.
        unsigned int _autoFlattenDataVersion = 0;
        float _autoFlattenDataQuiet = 0.0f;
        // Auto-flattening turns 3D OFF once it stops earning its cost - it is a transition OUT of
        // terrain, never a starting state. Until terrain has been reached once it cannot fire, or
        // a view whose DEM has not arrived flattens itself at startup and never recovers, because
        // flattening is what stops the elevation decode that would prove it wrong.
        bool _autoFlattenSeenTerrain = false;
        std::weak_ptr<TerrainOptions> _flattenSwitchOptions;

        std::shared_ptr<GLResourceManager> _glResourceManager;

        std::shared_ptr<CullWorker> _cullWorker;
        std::thread _cullThread;
        
        std::shared_ptr<VTLabelPlacementWorker> _vtLabelPlacementWorker;
        std::thread _vtLabelPlacementThread;
        
        std::shared_ptr<OptionsListener> _optionsListener;

        std::vector<std::pair<GLuint, GLuint> > _screenBoundFBOs;
        std::map<GLuint, std::shared_ptr<FrameBuffer> > _screenFrameBuffers;
        std::shared_ptr<Shader> _screenBlendShader;

        std::shared_ptr<PostProcessEffect> _postProcessEffect;
        std::shared_ptr<Shader> _postProcessShader;
        std::string _postProcessShaderName;
        std::optional<std::chrono::steady_clock::time_point> _postProcessStartTime;
        std::unique_ptr<TerrainRenderer> _terrainRenderer;
        std::weak_ptr<ElevationManager> _redrawElevationManager; // the one whose loads ask for a frame
        std::vector<vt::TileId> _groundCoverTileIds; // last frame's shared ground cover (shadow refresh trigger)
        std::unique_ptr<TerrainDrapeCache> _terrainDrapeCache;
        std::unique_ptr<TerrainShadowMap> _terrainShadowMap; // shared cross-layer drape target
        // What the shadow map currently holds. The caster pass is a second full draw of the
        // terrain, and the light box is snapped to a world lattice so its matrix repeats exactly
        // while the camera moves inside one texel step: while these match, the existing map is
        // still the right one and the pass is skipped.
        // Camera pose the last drape-bake pass ran against, to tell a moving frame from a
        // still one (see the bake time budget in onDrawFrame).
        cglib::mat4x4<double> _drapeBakeLastMVPMatrix = cglib::mat4x4<double>::identity();
        std::unique_ptr<ScreenMaskBuffer> _terrainShadowMaskBuffer;
        std::unique_ptr<ScreenMaskBuffer> _groundAOMaskBuffer;
        // Depth of the 3D occluders, for per-label occlusion (see the pass in drawLayers).
        std::unique_ptr<ScreenMaskBuffer> _labelOcclusionBuffer;
        std::unique_ptr<ScreenMaskBuffer> _groundAODrapeBuffer;
        bool _shadowMapValid = false;
        int _shadowMapSize = 0;
        int _shadowMapCascades = 0;
        int _shadowMapAge = 0;
        float _shadowMapFadeSignature = 0.0f;
        std::array<cglib::mat4x4<double>, 4> _shadowMapViewProjs;
        std::array<float, 4> _shadowMapBiases = { };
        // Per cascade: the pages are refreshed independently, and the outer one - which holds most
        // of the casters - keeps its box over far more camera movement than the near one.
        std::array<std::vector<vt::TileId>, 4> _shadowMapCasterTiles;

        unsigned int _layersElevationVersion = 0;
        std::optional<std::chrono::steady_clock::time_point> _lastElevationRefreshTime;
        // When the camera last moved, for the drape bake budget: a gesture's end keeps the
        // moving budget for a settle window, so a chain of quick zooms stays smooth.
        std::chrono::steady_clock::time_point _drapeBakeLastMoveTime = std::chrono::steady_clock::time_point();

        // Render thread only: the layers held back for drawOverlayLayers this frame, and whether
        // the effect resolved into the screen framebuffer's secondary color texture.
        std::vector<std::shared_ptr<Layer> > _overlayLayers;
        bool _postProcessSecondaryActive = false;

        // Render thread only: this frame's merged style opinion and the fog resolved from it,
        // computed before the sky and reused by every consumer so they cannot disagree.
        StyleEnvironment _frameStyleEnvironment;
        ResolvedFog _frameFog;

        BackgroundRenderer _backgroundRenderer;
        SkyRenderer _skyRenderer;
        
        std::vector<std::shared_ptr<BillboardDrawData> > _billboardDrawDatas;
        std::vector<std::shared_ptr<BillboardDrawData> > _billboardDrawDataBuffer;
        std::shared_ptr<BillboardPlacementWorker> _billboardPlacementWorker;
        std::thread _billboardPlacementThread;
    
        AnimationHandler _animationHandler;
        KineticEventHandler _kineticEventHandler;
        
        const std::shared_ptr<Layers> _layers;
        const std::shared_ptr<Options> _options;
        
        mutable std::atomic<bool> _surfaceCreated;
        mutable std::atomic<bool> _surfaceChanged;
        mutable std::atomic<bool> _billboardsChanged;
        mutable std::atomic<bool> _redrawPending;
        // Frames still owed after a redraw request, so a change reaches the FRONT buffer and not
        // only the back one (see requestRedraw).
        mutable std::atomic<int> _redrawExtraFrames;

        ThreadSafeDirectorPtr<RedrawRequestListener> _redrawRequestListener;

        ThreadSafeDirectorPtr<MapRendererListener> _mapRendererListener;

        std::vector<std::pair<DirectorPtr<RendererCaptureListener>, bool> > _rendererCaptureListeners;
        mutable std::mutex _rendererCaptureListenersMutex;

        std::vector<std::shared_ptr<OnChangeListener> > _onChangeListeners;
        mutable std::mutex _onChangeListenersMutex;

        mutable std::recursive_mutex _mutex;
    };
    
}

#endif

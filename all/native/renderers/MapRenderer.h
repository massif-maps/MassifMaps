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
#include "terrain/FlattenSwitchTimeline.h"
#include "ui/MapMoveReason.h"

#include <cglib/mat.h>
#include <cglib/ray.h>
#include <vt/TileId.h>

#include <array>
#include <atomic>
#include <limits>
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
    class ElevationTextureCache;
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

        /**
         * Forgets that the camera has been moved, so the auto-flatten rule ignores the SDK's initial view.
         * See AutoFlatten.
         */
        void resetCameraPlaced();

        /**
         * Holds the view against the render thread so a sequence of camera calls (moveTo) lands in one
         * frame, never a half-applied view. Recursive: the camera calls take it again inside.
         */
        std::unique_lock<std::recursive_mutex> holdView() const;

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
         * Returns the last view state published by a frame, without waiting for the renderer mutex.
         * For the application's thread: _mutex is held for a whole frame, and waiting on it can deadlock
         * against an event the frame emits. Up to a frame stale.
         * @return The last published view state.
         */
        ViewState getViewStateSnapshot() const;

        /**
         * Returns the current projection surface object.
         * @return The current projection surface object.
         */
        std::shared_ptr<ProjectionSurface> getProjectionSurface() const;
    
        /**
         * Requests the renderer to refresh the view. Normally not needed, the SDK redraws when needed.
         * The defaulted arguments record the call site for logRedrawSources; callers pass nothing.
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
         * The elevation texture cache: one per map, shared by every tile layer. GL thread only. Internal method.
         */
        std::shared_ptr<ElevationTextureCache> getElevationTextureCache(const std::shared_ptr<ElevationManager>& elevationManager);

        /**
         * Returns the terrain renderer (may be null). GL thread only. Internal method.
         */
        TerrainRenderer* getTerrainRenderer() const { return _terrainRenderer.get(); }

        /**
         * This frame's fog, resolved once from the options and the merged style before anything draws.
         * GL thread only. Internal method.
         */
        const ResolvedFog& getFrameFog() const { return _frameFog; }
        /**
         * The sun the last shadow pass cast from, towards the sun; false while no shadows are drawn.
         * Any thread. Internal method.
         */
        bool getShadowSunDir(cglib::vec3<float>& sunDir) const;

        std::vector<std::shared_ptr<BillboardDrawData> > getBillboardDrawDatas() const;
    
        AnimationHandler& getAnimationHandler();
        KineticEventHandler& getKineticEventHandler();

        // An animated call reports reason once, here; the frames it produces report ANIMATION.
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

        // depthTexture: the depth as a texture, so a post-process effect can read it (see
        // applyPostProcessEffect). Keyed apart from the renderbuffer one.
        void clearAndBindScreenFBO(const Color& color, bool depth, bool stencil, bool depthTexture = false);
        void blendAndUnbindScreenFBO(float opacity);
        // Full-screen quad sampling the mask. Sets no render state: one caller runs inside the drape bake.
        void drawMaskQuad(unsigned int texture, float invWidth, float invHeight);
        // dst *= mask, with its own render state.
        void multiplyScreenMask(unsigned int texture, float invWidth, float invHeight);
        void setZBuffering(bool enable);
    
        void calculateRayIntersectedElements(const MapPos& targetPos, ViewState& viewState, std::vector<RayIntersectedElement>& results);
        // For a ray that never meets the ground; sky-anchored layers (CelestialLayer) are only reachable this way.
        void calculateRayIntersectedElements(const cglib::ray3<double>& ray, ViewState& viewState, std::vector<RayIntersectedElement>& results);
    
        void billboardsChanged();
        void vtLabelsChanged(const std::shared_ptr<Layer>& layer, bool delay);
        void layerChanged(const std::shared_ptr<Layer>& layer, bool delay);
        void viewChanged(bool delay, MapMoveReason::MapMoveReason reason);
    
        void registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        void unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        
    private:
        // debug.massif.background 0 drops the map background plane. Android demo builds only.
        static bool isBackgroundEnabled();
        class OptionsListener : public Options::OnChangeListener {
        public:
            explicit OptionsListener(const std::shared_ptr<MapRenderer>& mapRenderer);
            
            virtual void onOptionChanged(const std::string& optionName);
            
        private:
            std::weak_ptr<MapRenderer> _mapRenderer;
        };

        void initializeRenderState() const;

        // Per camera event rather than one frame later (mapbox's transform._constrainCamera). Call with _mutex held.
        void constrainCameraToClearance();

        // First person: the ground under the eye, eased when a finer elevation level replaces the one that answered.
        double settleEyeGround(const ElevationManager& elevationManager, const MapPos& cameraMapPos, double groundZ, int groundZoom, float deltaSeconds);

        // Dumps and resets the per-call-site redraw counts: tells which caller keeps the map rendering.
        static void logRedrawSources();

        // Every tile layer's Map block merged, first definer wins; once per frame so everything fogs alike.
        StyleEnvironment collectStyleEnvironment(const ViewState& viewState) const;

        // With postProcessing, layers that opted out of the effect are held back for drawOverlayLayers.
        void drawLayers(float deltaSeconds, const ViewState& viewState, bool postProcessing);

        // Drawn after the effect resolves, on the same depth buffer so the terrain still occludes them.
        void drawOverlayLayers(float deltaSeconds, const ViewState& viewState);

        // True if tileId is a strict ancestor of other.
        static bool coversTile(const vt::TileId& tileId, const vt::TileId& other);

        // One non-overlapping quadtree partition shared by the layer stack; overlapping tesselations z-fight.
        // extendSeedsOnly (the drape) keeps the seed to levels the layers do not reach.
        void collectTerrainCover(const std::vector<std::shared_ptr<TileLayer> >& tileLayers, const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::vector<vt::TileId>& seedTileIds, bool extendSeedsOnly, std::vector<std::map<vt::TileId, std::size_t> >& layerTiles, std::map<vt::TileId, std::size_t>& collectedTiles, std::vector<vt::TileId>& leaves, int& coverZoom, int& maxCollectedZoom);

        // Camera-driven seed for collectTerrainCover: reaches floor(camera zoom) whatever a data source's max zoom.
        std::vector<vt::TileId> collectTerrainCoverTileIds(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions) const;

        // Re-renders the caster pass only on a real change; contentChanged rations content-driven refreshes.
        void applyTerrainShadows(const std::vector<std::shared_ptr<TileLayer> >& tileLayers, const std::vector<vt::TileId>& coverTileIds, const std::shared_ptr<TerrainOptions>& terrainOptions, const ViewState& viewState, int prevFBO, bool contentChanged, bool castShadows, ResolvedLighting& lighting, std::array<double, 4>& shadowTexelMeters);

        // keepBound resolves into the screen framebuffer's secondary color texture and leaves it bound, for overlays.
        void applyPostProcessEffect(const std::shared_ptr<PostProcessEffect>& effect, const ViewState& viewState, bool keepBound = false);

        void handleRendererCaptureCallbacks();

        // Screen displacement of the highest ground in view, pixels.
        double calculateTerrainParallax(const std::shared_ptr<TerrainOptions>& terrainOptions) const;
        // Returns true when the terrain decode state changed, the only time the visible tile set must be recomputed.
        bool updateTerrainFlatten(float deltaSeconds);
        void reportFlattenSwitchTiming(const FlattenSwitch::State& state, const FlattenSwitch::Input& input, int tilesOwed, float deltaSeconds);

        // A screen FBO key bit marking the depth-texture variant; no GL buffer mask uses bit 0.
        static const unsigned int SCREEN_FBO_DEPTH_TEXTURE_BIT = 1;
        static const int BILLBOARD_PLACEMENT_TASK_DELAY;
        static const int VT_LABEL_PLACEMENT_TASK_DELAY;
        // Zoom change that triggers its own label placement pass (see viewChanged).
        static const float LABEL_PLACEMENT_ZOOM_THRESHOLD;
        // Zoom drift before a drape tile is re-baked, so zoom-dependent style widths follow the camera.
        static const float DRAPE_REBAKE_ZOOM_THRESHOLD;
        static const int LABEL_PLACEMENT_ZOOM_DELAY;

        static const int ELEVATION_REFRESH_DELAY; // milliseconds between vector layer refreshes caused by elevation data changes
        static const float EYE_GROUND_SETTLE_TIME; // seconds for the first person eye to glide onto a refined ground
        static const float TERRAIN_SWITCH_WARM_TIMEOUT; // seconds the 2D/3D switch waits for the tiles 3D needs

        static const std::string BLEND_VERTEX_SHADER;
        static const std::string BLEND_FRAGMENT_SHADER;
        static const std::string POST_PROCESS_VERTEX_SHADER;
        
        std::optional<std::chrono::steady_clock::time_point> _lastFrameTime;
    
        ViewState _viewState;
        void publishViewStateSnapshot(const ViewState& viewState) const;
        mutable std::shared_ptr<const ViewState> _viewStateSnapshot;
        mutable std::mutex _viewStateSnapshotMutex; // pointer swap only, never held across work; not _mutex
        float _lastLabelPlacementZoom = 0.0f;

        // Phase of the 2D/3D switch; the ratio and decode state live on TerrainOptions.
        FlattenSwitch::State _flattenSwitchState;
        AutoFlatten::Trigger _autoFlattenTrigger;
        // Hold auto-flatten off until the DEM data version is still for TERRAIN_SWITCH_WARM_TIMEOUT.
        unsigned int _autoFlattenDataVersion = 0;
        float _autoFlattenDataQuiet = 0.0f;
        // Auto-flatten only leaves terrain; before it is reached, a view without DEM would flatten for good.
        bool _autoFlattenSeenTerrain = false;
        std::weak_ptr<TerrainOptions> _flattenSwitchOptions;
        FlattenSwitchTimeline _flattenSwitchTimeline;
        // Written by the draw pass, read by the next frame's switch. Render thread.
        bool _drapeBakesPending = false;
        int _drapeBakesDone = 0;
        // Set by every camera event; auto-flatten stays quiet while false.
        std::atomic<bool> _cameraPlaced { false };

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
        std::weak_ptr<ElevationManager> _redrawElevationManager;
        std::shared_ptr<ElevationTextureCache> _elevationTextureCache; // see getElevationTextureCache
        std::weak_ptr<ElevationManager> _elevationTextureCacheManager;
        std::vector<vt::TileId> _groundCoverTileIds; // last frame's shared ground cover (shadow refresh trigger)
        std::unique_ptr<TerrainDrapeCache> _terrainDrapeCache;
        std::unique_ptr<TerrainShadowMap> _terrainShadowMap; // shared cross-layer drape target

        // Camera pose of the last drape-bake pass, to tell a moving frame from a still one.
        cglib::mat4x4<double> _drapeBakeLastMVPMatrix = cglib::mat4x4<double>::identity();
        // Quantised bake zoom, held while the camera moves (as mapbox) so a gesture does not re-bake every step.
        std::size_t _drapeBakeZoomTerm = 0;
        std::unique_ptr<ScreenMaskBuffer> _terrainShadowMaskBuffer;
        std::unique_ptr<ScreenMaskBuffer> _groundAOMaskBuffer;
        // Depth of the 3D occluders, for per-label occlusion (see the pass in drawLayers).
        std::unique_ptr<ScreenMaskBuffer> _labelOcclusionBuffer;
        std::unique_ptr<ScreenMaskBuffer> _groundAODrapeBuffer;
        bool _shadowMapValid = false;
        mutable std::mutex _shadowSunMutex;
        bool _shadowSunActive = false;
        cglib::vec3<float> _shadowSunDir = cglib::vec3<float>(0, 0, 1);
        int _shadowMapSize = 0;
        int _shadowMapCascades = 0;
        int _shadowMapAge = 0;
        // Per cascade: caster content signature when that page was last drawn.
        std::array<float, 4> _shadowMapFadeSignatures = { };
        std::array<cglib::mat4x4<double>, 4> _shadowMapViewProjs;
        // Per cascade, since pages refresh independently.
        std::array<std::vector<vt::TileId>, 4> _shadowMapCasterTiles;

        unsigned int _layersElevationVersion = 0;
        // settleEyeGround's state: the level that answered last frame, and the eye's ground minus that answer.
        int _eyeGroundZoom = -1;
        double _eyeGroundZ = 0;
        double _eyeGroundOffset = 0;
        double _eyeGroundTarget = 0; // the answer the glide heads for, and where it was asked
        double _eyeGroundX = 0;
        double _eyeGroundY = 0;
        std::optional<std::chrono::steady_clock::time_point> _lastElevationRefreshTime;
        // The moving bake budget lasts a settle window past a gesture, so chained quick zooms stay smooth.
        std::chrono::steady_clock::time_point _drapeBakeLastMoveTime = std::chrono::steady_clock::time_point();

        // Render thread only.
        std::vector<std::shared_ptr<Layer> > _overlayLayers;
        bool _postProcessSecondaryActive = false;

        // Render thread only; computed before the sky so every consumer agrees.
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
        // Frames still owed after a redraw request, so a change reaches the front buffer too.
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

#include "MapRenderer.h"
#include "components/Exceptions.h"
#include "components/Layers.h"
#include "components/ThreadWorker.h"
#include "core/MapPos.h"
#include "core/ScreenPos.h"
#include "core/ScreenBounds.h"
#include "graphics/Bitmap.h"
#include "layers/Layer.h"
#include "layers/TileLayer.h"
#include "layers/VectorLayer.h"
#include "layers/VectorTileLayer.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "renderers/BillboardRenderer.h"
#include "renderers/MapRendererListener.h"
#include "renderers/RendererCaptureListener.h"
#include "renderers/RedrawRequestListener.h"
#include "renderers/TileRenderer.h"
#include "renderers/components/BillboardSorter.h"
#include "renderers/components/RayIntersectedElement.h"
#include "renderers/cameraevents/CameraPanEvent.h"
#include "renderers/cameraevents/CameraRotationEvent.h"
#include "renderers/cameraevents/CameraTiltEvent.h"
#include "renderers/cameraevents/CameraZoomEvent.h"
#include "renderers/drawdatas/BillboardDrawData.h"
#include "renderers/utils/GLContext.h"
#include "renderers/utils/GLResourceManager.h"
#include "renderers/utils/FrameBuffer.h"
#include "renderers/PostProcessEffect.h"
#include "renderers/TerrainRenderer.h"
#include "renderers/utils/TerrainDrapeCache.h"
#include "renderers/utils/TerrainShadowMap.h"
#include "renderers/utils/ScreenMaskBuffer.h"

#include <chrono>
#include <set>
#include "core/MapTile.h"
#include "terrain/AutoFlatten.h"
#include "terrain/CameraClearance.h"
#include "terrain/DrapeStackCuts.h"
#include "terrain/DrapeTuning.h"
#include "terrain/ElevationManager.h"
#include "renderers/utils/Shader.h"
#include "renderers/utils/Texture.h"
#include "renderers/workers/BillboardPlacementWorker.h"
#include "renderers/workers/VTLabelPlacementWorker.h"
#include "renderers/workers/CullWorker.h"
#include "utils/Const.h"
#include "utils/FrameProfiler.h"
#include "utils/Log.h"
#include "components/FogOptions.h"

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif
#include "utils/ThreadUtils.h"

#include <vt/RenderStats.h>
#include <vt/GLTileRenderer.h> // the shadow cutout/fade constants, so the fade is not a second 4.5

#include <algorithm>
#include <limits>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace massif {

#if MASSIF_VT_RENDER_STATS
    namespace {
        // Diagnostic dump of the vt label/tile churn counters, compiled in together with the
        // counters themselves (see vt/RenderStats.h - MASSIF_VT_RENDER_STATS is the only
        // switch). Everything except 'live' is a per-interval delta. Only called from the GL
        // thread, so the previous values need no synchronization; the counters themselves are
        // atomic because the placement worker and the tile threads also increment them.
        constexpr int RENDER_STATS_INTERVAL = 1000; // ms

        void logRenderStats() {
            using vt::RenderStats;

            static const int COUNT = 17;
            static std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
            static long long lastValues[COUNT] = { 0 };

            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            if (now - lastTime < std::chrono::milliseconds(RENDER_STATS_INTERVAL)) {
                return;
            }
            lastTime = now;

            const long long values[COUNT] = {
                RenderStats::visibleTileSetChanges.load(),
                RenderStats::tileSurfacesBuilt.load(),
                RenderStats::tileSurfacesInvalidated.load(),
                RenderStats::labelsAllocated.load(),
                RenderStats::labelElevationReanchors.load(),
                RenderStats::placementUpdates.load(),
                RenderStats::placementReanchorsNull.load(),
                RenderStats::placementReanchorsHidden.load(),
                RenderStats::placementReanchorsVisible.load(),
                RenderStats::snapPlacements.load(),
                RenderStats::snapPlacementsMoved.load(),
                RenderStats::labelMapRebuilds.load(),
                RenderStats::labelsReused.load(),
                RenderStats::cullWorkerUpdates.load(),
                RenderStats::tileRecalculations.load(),
                RenderStats::tileLayersSkipped.load(),
                RenderStats::placementSearches.load()
            };
            long long deltas[COUNT];
            for (int i = 0; i < COUNT; i++) {
                deltas[i] = values[i] - lastValues[i];
                lastValues[i] = values[i];
            }
            static long long lastPasses = 0, lastFlips = 0, lastCullerNs = 0;
            long long passes = RenderStats::cullerPasses.load();
            long long flips = RenderStats::cullerVisibilityFlips.load();
            long long cullerNs = RenderStats::cullerNs.load();
            long long deltaPasses = passes - lastPasses;
            long long deltaFlips = flips - lastFlips;
            long long deltaCullerNs = cullerNs - lastCullerNs;
            lastPasses = passes;
            lastFlips = flips;
            lastCullerNs = cullerNs;

            Log::Infof("RenderStats: cullUpd=%lld tileRecalc=%lld tileSkip=%lld tileSets=%lld labelMaps=%lld | surfBuilt=%lld surfInval=%lld | labelsAlloc=%lld reused=%lld live=%lld elevReanchor=%lld | placeUpd=%lld reNull=%lld reHidden=%lld reVisible=%lld search=%lld | snap=%lld snapMoved=%lld | cullPasses=%lld visFlips=%lld cullMs=%.2f",
                       deltas[13], deltas[14], deltas[15], deltas[0], deltas[11],
                       deltas[1], deltas[2],
                       deltas[3], deltas[12], RenderStats::labelsLive.load(), deltas[4],
                       deltas[5], deltas[6], deltas[7], deltas[8], deltas[16],
                       deltas[9], deltas[10], deltaPasses, deltaFlips, deltaCullerNs / 1.0e6);

            // Draw submission, per interval. geomDraws is the number that matters: the frame
            // cost of a style tracks it, not the index count next to it.
            static long long lastDraws = 0, lastIndices = 0, lastLabelDraws = 0, lastTiles = 0, lastStyleLayers = 0;
            long long draws = RenderStats::geometryDraws.load();
            long long indices = RenderStats::geometryIndices.load();
            long long labelDraws = RenderStats::labelDraws.load();
            long long tiles = RenderStats::renderTilesDrawn.load();
            long long styleLayers = RenderStats::styleLayersDrawn.load();
            static long long lastSurfaceDraws = 0, lastSurfaceIndices = 0;
            long long surfaceDraws = RenderStats::surfaceDraws.load();
            long long surfaceIndices = RenderStats::surfaceIndices.load();
            Log::Infof("RenderStats: geomDraws=%lld geomIndices=%lld labelDraws=%lld renderTiles=%lld styleLayers=%lld surfDraws=%lld surfIndices=%lld (per interval)",
                       draws - lastDraws, indices - lastIndices, labelDraws - lastLabelDraws,
                       tiles - lastTiles, styleLayers - lastStyleLayers,
                       surfaceDraws - lastSurfaceDraws, surfaceIndices - lastSurfaceIndices);
            lastSurfaceDraws = surfaceDraws; lastSurfaceIndices = surfaceIndices;

            static long long lastSurfSplit[7] = { 0 };
            const long long surfSplit[7] = {
                RenderStats::surfShadowDraws.load(), RenderStats::surfMaskDraws.load(),
                RenderStats::surfFillDraws.load(), RenderStats::surfBlitDraws.load(),
                RenderStats::surfDrapeDraws.load(), RenderStats::surfBackgroundDraws.load(),
                RenderStats::surfBitmapDraws.load()
            };
            Log::Infof("RenderStats: surfaces shadow=%lld mask=%lld fill=%lld blit=%lld drape=%lld background=%lld bitmap=%lld (per interval)",
                       surfSplit[0] - lastSurfSplit[0], surfSplit[1] - lastSurfSplit[1],
                       surfSplit[2] - lastSurfSplit[2], surfSplit[3] - lastSurfSplit[3],
                       surfSplit[4] - lastSurfSplit[4], surfSplit[5] - lastSurfSplit[5],
                       surfSplit[6] - lastSurfSplit[6]);
            for (int i = 0; i < 7; i++) {
                lastSurfSplit[i] = surfSplit[i];
            }
            static long long lastMaskNs = 0, lastDrapeNs = 0;
            long long maskNs = RenderStats::surfMaskNs.load();
            long long drapeNs = RenderStats::surfDrapeNs.load();
            Log::Infof("RenderStats: surfaces maskMs=%.1f drapeMs=%.1f (per interval)",
                       (maskNs - lastMaskNs) / 1.0e6, (drapeNs - lastDrapeNs) / 1.0e6);
            lastMaskNs = maskNs; lastDrapeNs = drapeNs;

            static long long lastLabelBuild = 0, lastLabelBatch = 0, lastLabelVerts = 0, lastLineLayouts = 0;
            long long labelBuild = RenderStats::labelVertexBuildNs.load();
            long long labelBatch = RenderStats::labelBatchNs.load();
            long long labelVerts = RenderStats::labelsDrawnVertices.load();
            long long lineLayouts = RenderStats::lineLayoutBuilds.load();
            Log::Infof("RenderStats: labels built=%lld lineLayouts=%lld buildMs=%.1f batchMs=%.1f (per interval)",
                       labelVerts - lastLabelVerts, lineLayouts - lastLineLayouts,
                       (labelBuild - lastLabelBuild) / 1.0e6,
                       (labelBatch - lastLabelBatch) / 1.0e6);
            lastLabelBuild = labelBuild; lastLabelBatch = labelBatch; lastLabelVerts = labelVerts;
            lastLineLayouts = lineLayouts;

            // The tile-set change path, which runs INSIDE the layer draw pass and was untimed.
            // refreshMs is the total and includes the setVisibleTiles split that follows it.
            static long long lastTileSet[9] = { 0 };
            const long long tileSet[9] = {
                RenderStats::refreshTilesLockNs.load(), RenderStats::refreshTilesNs.load(),
                RenderStats::setVisibleTilesLockNs.load(), RenderStats::terrainCoarseningNs.load(),
                RenderStats::tileSurfacesNs.load(), RenderStats::labelMapsNs.load(),
                RenderStats::renderTilesNs.load(), RenderStats::spanUnionsNs.load(),
                RenderStats::labelAnchorNs.load()
            };
            Log::Infof("RenderStats: tileSetChange refreshLockMs=%.1f refreshMs=%.1f | "
                       "setVisibleLockMs=%.1f coarsenMs=%.1f surfacesMs=%.1f labelMapsMs=%.1f "
                       "renderTilesMs=%.1f spanUnionsMs=%.1f labelAnchorMs=%.1f (per interval)",
                       (tileSet[0] - lastTileSet[0]) / 1.0e6, (tileSet[1] - lastTileSet[1]) / 1.0e6,
                       (tileSet[2] - lastTileSet[2]) / 1.0e6, (tileSet[3] - lastTileSet[3]) / 1.0e6,
                       (tileSet[4] - lastTileSet[4]) / 1.0e6, (tileSet[5] - lastTileSet[5]) / 1.0e6,
                       (tileSet[6] - lastTileSet[6]) / 1.0e6, (tileSet[7] - lastTileSet[7]) / 1.0e6,
                       (tileSet[8] - lastTileSet[8]) / 1.0e6);
            for (int i = 0; i < 9; i++) { lastTileSet[i] = tileSet[i]; }

            // buildLabelMaps by phase. signature/list run whatever happens; merge/carry are the
            // ones reuse actually shortens.
            static long long lastLabelMap[6] = { 0 };
            const long long labelMap[6] = {
                RenderStats::labelSignatureNs.load(), RenderStats::labelMergeNs.load(),
                RenderStats::labelStampNs.load(), RenderStats::labelReleaseNs.load(),
                RenderStats::labelCarryNs.load(), RenderStats::labelListNs.load()
            };
            Log::Infof("RenderStats: buildLabelMaps signatureMs=%.1f mergeMs=%.1f stampMs=%.1f "
                       "releaseMs=%.1f carryMs=%.1f listMs=%.1f (per interval)",
                       (labelMap[0] - lastLabelMap[0]) / 1.0e6, (labelMap[1] - lastLabelMap[1]) / 1.0e6,
                       (labelMap[2] - lastLabelMap[2]) / 1.0e6, (labelMap[3] - lastLabelMap[3]) / 1.0e6,
                       (labelMap[4] - lastLabelMap[4]) / 1.0e6, (labelMap[5] - lastLabelMap[5]) / 1.0e6);
            for (int i = 0; i < 6; i++) { lastLabelMap[i] = labelMap[i]; }

            // Why reuse missed. 'spanning' counts labels fed by more than one tile - an unclipped
            // label (text-clip: false) is one, and its signature covers every contributing tile.
            static long long lastMiss[5] = { 0 };
            const long long miss[5] = {
                RenderStats::labelMissNew.load(), RenderStats::labelMissCount.load(),
                RenderStats::labelMissHash.load(), RenderStats::labelMissSpanning.load(),
                RenderStats::labelHitSpanning.load()
            };
            Log::Infof("RenderStats: labelReuse missNew=%lld missCount=%lld missHash=%lld | "
                       "spanning miss=%lld hit=%lld (per interval)",
                       miss[0] - lastMiss[0], miss[1] - lastMiss[1], miss[2] - lastMiss[2],
                       miss[3] - lastMiss[3], miss[4] - lastMiss[4]);
            for (int i = 0; i < 5; i++) { lastMiss[i] = miss[i]; }

            // What construction costs, and how much of it was for a label never shown.
            static long long lastCtor[3] = { 0 };
            const long long ctor[3] = {
                RenderStats::labelConstructNs.load(), RenderStats::labelRetiredSeen.load(),
                RenderStats::labelRetiredUnseen.load()
            };
            static long long lastMergeIters = 0;
            long long mergeIters = RenderStats::labelMergeIterations.load();
            Log::Infof("RenderStats: labelConstruct totalMs=%.1f | retired seen=%lld unseen=%lld | mergeIterations=%lld (per interval)",
                       (ctor[0] - lastCtor[0]) / 1.0e6, ctor[1] - lastCtor[1], ctor[2] - lastCtor[2],
                       mergeIters - lastMergeIters);
            for (int i = 0; i < 3; i++) { lastCtor[i] = ctor[i]; }
            lastMergeIters = mergeIters;

            static long long lastPrep[4] = { 0 }, lastLabelSplit[2] = { 0 }, lastLabelXf = 0, lastLabelAttr = 0;
            const long long prep[4] = {
                RenderStats::prepTileBlendNs.load(), RenderStats::prepElevDirtyNs.load(),
                RenderStats::prepElevUpdateNs.load(), RenderStats::prepLabelBlendNs.load()
            };
            const long long labelSplit[2] = {
                RenderStats::labelPlacementNs.load(), RenderStats::labelLineBuildNs.load()
            };
            Log::Infof("RenderStats: prepare tileBlendMs=%.1f elevDirtyMs=%.1f elevUpdMs=%.1f labelBlendMs=%.1f | labelBuild placementMs=%.1f lineMs=%.1f transformMs=%.1f attribMs=%.1f (per interval)",
                       (prep[0] - lastPrep[0]) / 1.0e6, (prep[1] - lastPrep[1]) / 1.0e6,
                       (prep[2] - lastPrep[2]) / 1.0e6, (prep[3] - lastPrep[3]) / 1.0e6,
                       (labelSplit[0] - lastLabelSplit[0]) / 1.0e6, (labelSplit[1] - lastLabelSplit[1]) / 1.0e6,
                       (RenderStats::labelTransformNs.load() - lastLabelXf) / 1.0e6,
                       (RenderStats::labelAttribNs.load() - lastLabelAttr) / 1.0e6);
            lastLabelXf = RenderStats::labelTransformNs.load();
            lastLabelAttr = RenderStats::labelAttribNs.load();
            for (int i = 0; i < 4; i++) { lastPrep[i] = prep[i]; }
            for (int i = 0; i < 2; i++) { lastLabelSplit[i] = labelSplit[i]; }

            static long long lastPass3D[3] = { 0 };
            const long long pass3D[3] = {
                RenderStats::pass3DLabels2DNs.load(), RenderStats::pass3DGeometryNs.load(),
                RenderStats::pass3DLabels3DNs.load()
            };
            Log::Infof("RenderStats: pass3D labels2DMs=%.1f geometryMs=%.1f labels3DMs=%.1f (per interval)",
                       (pass3D[0] - lastPass3D[0]) / 1.0e6, (pass3D[1] - lastPass3D[1]) / 1.0e6,
                       (pass3D[2] - lastPass3D[2]) / 1.0e6);
            for (int i = 0; i < 3; i++) { lastPass3D[i] = pass3D[i]; }

            static long long lastEndFrame = 0, lastSwept = 0;
            long long endFrameNs = RenderStats::endFrameNs.load();
            long long swept = RenderStats::endFrameSwept.load();
            static long long lastMutexWait = 0;
            long long mutexWait = RenderStats::mutexWaitNs.load();
            static long long lastBakes = 0, lastBakeNs = 0, lastQueued = 0;
            long long bakes = RenderStats::drapeBakes.load();
            long long bakeNs = RenderStats::drapeBakeNs.load();
            long long queued = RenderStats::drapeBakeQueued.load();
            Log::Infof("RenderStats: drape bakes=%lld queued=%lld totalMs=%.1f msPerBake=%.1f (per interval)",
                       bakes - lastBakes, queued - lastQueued, (bakeNs - lastBakeNs) / 1.0e6,
                       (bakeNs - lastBakeNs) / 1.0e6 / std::max(1LL, bakes - lastBakes));
            lastBakes = bakes; lastBakeNs = bakeNs; lastQueued = queued;
            // The elevation texture pipeline, which is what extra DEM detail multiplies.
            static long long lastDem[6] = { 0 };
            const long long dem[6] = {
                RenderStats::demEncodes.load(), RenderStats::demBorderPatches.load(),
                RenderStats::demEncodeNs.load(), RenderStats::demUploads.load(),
                RenderStats::demUploadNs.load(), RenderStats::demPatchNs.load()
            };
            Log::Infof("RenderStats: dem encodes=%lld patches=%lld encodeMs=%.1f | uploads=%lld uploadMs=%.1f patchMs=%.1f | live=%lld resolved=%lld zoomGap=%lld (per interval)",
                       dem[0] - lastDem[0], dem[1] - lastDem[1], (dem[2] - lastDem[2]) / 1.0e6,
                       dem[3] - lastDem[3], (dem[4] - lastDem[4]) / 1.0e6, (dem[5] - lastDem[5]) / 1.0e6,
                       RenderStats::demTexturesLive.load(), RenderStats::demTexturesResolved.load(), RenderStats::demTileZoomGap.load());
            for (int i = 0; i < 6; i++) { lastDem[i] = dem[i]; }

            Log::Infof("RenderStats: endFrame ms=%.1f swept=%lld labelLockWaitMs=%.1f (per interval)",
                       (endFrameNs - lastEndFrame) / 1.0e6, swept - lastSwept,
                       (mutexWait - lastMutexWait) / 1.0e6);
            lastMutexWait = mutexWait;
            lastEndFrame = endFrameNs; lastSwept = swept;

            // Where one geometry draw goes, in microseconds. 'skips' are calls that set up and
            // then found the style invisible - they pay everything up to their bail-out point.
            static long long lastProgram = 0, lastTerrain = 0, lastStyle = 0, lastStyleEval = 0, lastCompile = 0, lastBind = 0, lastDraw = 0, lastSkips = 0, lastMisses = 0;
            long long program = RenderStats::geomProgramNs.load();
            long long terrain = RenderStats::geomTerrainNs.load();
            long long style = RenderStats::geomStyleNs.load();
            long long styleEval = RenderStats::geomStyleEvalNs.load();
            long long compile = RenderStats::geomCompileNs.load();
            long long bind = RenderStats::geomBindNs.load();
            long long draw = RenderStats::geomDrawNs.load();
            long long skips = RenderStats::geometrySkips.load();
            long long misses = RenderStats::geomCompileMisses.load();
            static long long lastProbe = 0;
            long long probe = RenderStats::geomProbeNs.load();
            long long deltaCalls = std::max(1LL, (draws - lastDraws) + (skips - lastSkips));
            Log::Infof("RenderStats: perDraw us probe=%.2f program=%.1f terrain=%.1f styleEval=%.1f styleUpload=%.1f compile=%.1f bind=%.1f draw=%.1f (calls=%lld skips=%lld vboMisses=%lld)",
                       (probe - lastProbe) / 1000.0 / deltaCalls,
                       (program - lastProgram) / 1000.0 / deltaCalls, (terrain - lastTerrain) / 1000.0 / deltaCalls,
                       (styleEval - lastStyleEval) / 1000.0 / deltaCalls, (style - lastStyle) / 1000.0 / deltaCalls,
                       (compile - lastCompile) / 1000.0 / deltaCalls,
                       (bind - lastBind) / 1000.0 / deltaCalls, (draw - lastDraw) / 1000.0 / deltaCalls,
                       deltaCalls, skips - lastSkips, misses - lastMisses);
            Log::Infof("RenderStats: geomCompileStale=%lld (cumulative)", RenderStats::geomCompileStale.load());
            lastProgram = program; lastTerrain = terrain; lastStyle = style;
            lastStyleEval = styleEval; lastCompile = compile; lastBind = bind; lastDraw = draw;
            lastSkips = skips; lastMisses = misses; lastProbe = probe;

            static long long lastLookups = 0, lastFuncMisses = 0, lastConstants = 0, lastParams = 0;
            long long lookups = RenderStats::styleFuncLookups.load();
            long long funcMisses = RenderStats::styleFuncMisses.load();
            long long constants = RenderStats::styleFuncConstants.load();
            long long params = RenderStats::styleParameters.load();
            static long long lastFuncEval = 0;
            long long funcEval = RenderStats::styleFuncEvalNs.load();
            static long long lastViewStates = 0;
            long long viewStates = RenderStats::viewStateChanges.load();
            Log::Infof("RenderStats: styleFuncs lookups=%lld misses=%lld constants=%lld | params/draw=%.1f evalUsPerDraw=%.1f evalUsPerMiss=%.2f viewStates=%lld",
                       lookups - lastLookups, funcMisses - lastFuncMisses, constants - lastConstants,
                       (params - lastParams) / (double) deltaCalls,
                       (funcEval - lastFuncEval) / 1000.0 / deltaCalls,
                       (funcEval - lastFuncEval) / 1000.0 / std::max(1LL, funcMisses - lastFuncMisses),
                       viewStates - lastViewStates);
            lastViewStates = viewStates;
            lastLookups = lookups; lastFuncMisses = funcMisses; lastConstants = constants; lastParams = params;
            lastFuncEval = funcEval;

            lastDraws = draws;
            lastIndices = indices;
            lastLabelDraws = labelDraws;
            lastTiles = tiles;
            lastStyleLayers = styleLayers;
        }
    }
#endif

    MapRenderer::MapRenderer(const std::shared_ptr<Layers>& layers, const std::shared_ptr<Options>& options) :
        _lastFrameTime(),
        _viewState(),
        _glResourceManager(),
        _cullWorker(std::make_shared<CullWorker>()),
        _cullThread(),
        _vtLabelPlacementWorker(std::make_shared<VTLabelPlacementWorker>()),
        _vtLabelPlacementThread(),
        _optionsListener(),
        _screenBoundFBOs(),
        _screenFrameBuffers(),
        _screenBlendShader(),
        _backgroundRenderer(*options, *layers),
        _skyRenderer(*options),
        _billboardDrawDatas(),
        _billboardDrawDataBuffer(),
        _billboardPlacementWorker(std::make_shared<BillboardPlacementWorker>()),
        _billboardPlacementThread(),
        _animationHandler(*this),
        _kineticEventHandler(*this, *options),
        _layers(layers),
        _options(options),
        _surfaceCreated(false),
        _surfaceChanged(false),
        _billboardsChanged(false),
        _redrawPending(false),
        _redrawExtraFrames(0),
        _redrawRequestListener(),
        _mapRendererListener(),
        _rendererCaptureListeners(),
        _rendererCaptureListenersMutex(),
        _onChangeListeners(),
        _onChangeListenersMutex(),
        _mutex()
    {
    }
        
    MapRenderer::~MapRenderer() {
    }
        
    void MapRenderer::init() {
        _cullWorker->setComponents(shared_from_this(), _cullWorker);
        _cullThread = std::thread(std::ref(*_cullWorker));

        _vtLabelPlacementWorker->setComponents(shared_from_this(), _vtLabelPlacementWorker);
        _vtLabelPlacementThread = std::thread(std::ref(*_vtLabelPlacementWorker));

        _billboardPlacementWorker->setComponents(shared_from_this(), _billboardPlacementWorker);
        _billboardPlacementThread = std::thread(std::ref(*_billboardPlacementWorker));
        
        _optionsListener = std::make_shared<OptionsListener>(shared_from_this());
        _options->registerOnChangeListener(_optionsListener);
    }

    void MapRenderer::deinit() {
        _options->unregisterOnChangeListener(_optionsListener);
        _optionsListener.reset();
        
        _cullWorker->stop();
        _cullThread.detach();

        _vtLabelPlacementWorker->stop();
        _vtLabelPlacementThread.detach();
        
        _billboardPlacementWorker->stop();
        _billboardPlacementThread.detach();
    }
        
    std::shared_ptr<RedrawRequestListener> MapRenderer::getRedrawRequestListener() const {
         return _redrawRequestListener.get();
    }
        
    void MapRenderer::setRedrawRequestListener(const std::shared_ptr<RedrawRequestListener>& listener) {
        _redrawRequestListener.set(listener);
    }
        
    std::shared_ptr<MapRendererListener> MapRenderer::getMapRendererListener() const {
        return _mapRendererListener.get();
    }

    void MapRenderer::setMapRendererListener(const std::shared_ptr<MapRendererListener>& listener) {
        _mapRendererListener.set(listener);
    }

    ViewState MapRenderer::getViewState() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        ViewState viewState = _viewState;
        viewState.calculateViewState(*_options);
        return viewState;
    }

    std::shared_ptr<ProjectionSurface> MapRenderer::getProjectionSurface() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::shared_ptr<ProjectionSurface> projectionSurface = _viewState.getProjectionSurface();
        if (!projectionSurface) {
            projectionSurface = _options->getProjectionSurface();
        }
        return projectionSurface;
    }
        
    // Call-site tally for requestRedraw. Requests come from every thread (tile workers, placement
    // workers, the GL thread), so it is guarded; the cost is one short lock per request, against a
    // whole frame of work per request.
    static std::mutex redrawSourceMutex;
    static std::map<std::pair<const char*, int>, int> redrawSourceCounts;

    void MapRenderer::logRedrawSources() {
        std::map<std::pair<const char*, int>, int> counts;
        {
            std::lock_guard<std::mutex> lock(redrawSourceMutex);
            counts.swap(redrawSourceCounts);
        }
        std::vector<std::pair<int, std::pair<const char*, int> > > sorted;
        sorted.reserve(counts.size());
        for (auto it = counts.begin(); it != counts.end(); it++) {
            sorted.emplace_back(it->second, it->first);
        }
        std::sort(sorted.begin(), sorted.end(), [](const std::pair<int, std::pair<const char*, int> >& a, const std::pair<int, std::pair<const char*, int> >& b) {
            return a.first > b.first;
        });
        std::string summary;
        for (std::size_t i = 0; i < sorted.size() && i < 6; i++) {
            const char* file = sorted[i].second.first;
            const char* name = std::strrchr(file, '/');
            summary += (summary.empty() ? "" : ", ") + std::string(name ? name + 1 : file) + ":" + std::to_string(sorted[i].second.second) + " x" + std::to_string(sorted[i].first);
        }
        Log::Infof("MapRenderer: redraw requests by source - %s", summary.empty() ? "none" : summary.c_str());
    }

    void MapRenderer::requestRedraw(const char* callerFile, int callerLine) const {
        {
            std::lock_guard<std::mutex> lock(redrawSourceMutex);
            redrawSourceCounts[std::make_pair(callerFile, callerLine)]++;
        }

        DirectorPtr<RedrawRequestListener> redrawRequestListener = _redrawRequestListener;

        if (redrawRequestListener) {
            _redrawPending = true;
            // ONE drawn frame is not enough to put new content on screen. The surface is
            // double-buffered and RENDERMODE_WHEN_DIRTY draws exactly as many frames as were
            // requested, so a single frame lands in the back buffer and the front one - the
            // previous state - is what stays visible until something else happens to draw again.
            // Measured on the emulator by toggling FogOptions from adb: every change that drew two
            // frames appeared, and the one in eight that drew a single frame never did, permanently
            // (a second screenshot four seconds later was byte-identical), while the render thread
            // logged the correct fog for that frame. Most changes came out right only because a
            // cull pass happened to request a second redraw behind them.
            _redrawExtraFrames = 1;
            redrawRequestListener->onRedrawRequested();
        }
    }
    
    void MapRenderer::captureRendering(const std::shared_ptr<RendererCaptureListener>& listener, bool waitWhileUpdating) {
        if (!listener) {
            throw NullArgumentException("Null listener");
        }

        {
            std::lock_guard<std::mutex> lock(_rendererCaptureListenersMutex);
            _rendererCaptureListeners.push_back(std::make_pair(DirectorPtr<RendererCaptureListener>(listener), waitWhileUpdating));
        }
        requestRedraw();
    }

    std::shared_ptr<Layers> MapRenderer::getLayers() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _layers;
    }

    std::shared_ptr<Options> MapRenderer::getOptions() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _options;
    }

    std::shared_ptr<GLResourceManager> MapRenderer::getGLResourceManager() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _glResourceManager;
    }

    std::vector<std::shared_ptr<BillboardDrawData> > MapRenderer::getBillboardDrawDatas() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _billboardDrawDatas;
    }

    AnimationHandler& MapRenderer::getAnimationHandler() {
        return _animationHandler;
    }
    
    KineticEventHandler& MapRenderer::getKineticEventHandler() {
        return _kineticEventHandler;
    }
    
    void MapRenderer::calculateCameraEvent(CameraPanEvent& cameraEvent, float durationSeconds, bool updateKinetic, MapMoveReason::MapMoveReason reason) {
        if (durationSeconds > 0) {
            if (cameraEvent.isUseDelta()) {
                _animationHandler.setPanDelta(cameraEvent.getPosDelta(), durationSeconds);
            } else {
                _animationHandler.setPanTarget(cameraEvent.getPos(), durationSeconds);
            }
    
            // Animation will start on the next frame
            requestRedraw();
            return;
        }
    
        MapPos oldFocusPos;
        MapPos newFocusPos;
        float zoom;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            std::shared_ptr<ProjectionSurface> projectionSurface = getProjectionSurface();

            oldFocusPos = projectionSurface->calculateMapPos(_viewState.getFocusPos());
        
            // Calculate new focusPos, cameraPos and upVec
            cameraEvent.calculate(*_options, _viewState);
            _pannedSinceClearance = true;
    
            // Calculate parameters for kinetic events
            newFocusPos = projectionSurface->calculateMapPos(_viewState.getFocusPos());
            zoom = _viewState.getZoom();
          
            // In case of seamless panning horizontal teleport, offset the delta focus pos
            oldFocusPos.setX(oldFocusPos.getX() + _viewState.getHorizontalLayerOffsetDir() * Const::WORLD_SIZE);
        }
    
        // Delay updating the layers, because view state will be updated only after onDrawFrame is called
        viewChanged(true, reason);
    
        if (updateKinetic) {
            _kineticEventHandler.setPanDelta(std::make_pair(oldFocusPos, newFocusPos), zoom);
        } 
    }
        
    void MapRenderer::calculateCameraEvent(CameraRotationEvent& cameraEvent, float durationSeconds, bool updateKinetic, MapMoveReason::MapMoveReason reason) {
        if (durationSeconds > 0) {
            float oldRotation;
            {
                std::lock_guard<std::recursive_mutex> lock(_mutex);
                oldRotation = _viewState.getRotation();
            }
            _animationHandler.setRotationTarget(cameraEvent.isUseDelta() ? oldRotation + cameraEvent.getRotationDelta() : cameraEvent.getRotation(), cameraEvent.isUseTarget() ? &cameraEvent.getTargetPos() : nullptr, durationSeconds);
    
            // Animation will start on the next frame
            requestRedraw();
            return;
        }

        MapPos focusPos;
        float deltaRotation;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            float oldRotation = _viewState.getRotation();
            
            // Calculate new focusPos, cameraPos and upVec
            cameraEvent.calculate(*_options, _viewState);
            
            // Calculate parameters for kinetic events
            float rotation = _viewState.getRotation();
            deltaRotation = rotation - oldRotation;

            focusPos = getProjectionSurface()->calculateMapPos(_viewState.getFocusPos());
        }
    
        // Delay updating the layers, because view state will be updated only after onDrawFrame is called
        viewChanged(true, reason);
        
        if (updateKinetic) {
            _kineticEventHandler.setRotationDelta(deltaRotation, cameraEvent.isUseTarget() ? cameraEvent.getTargetPos() : focusPos);
        }
    }
        
    void MapRenderer::calculateCameraEvent(CameraTiltEvent& cameraEvent, float durationSeconds, bool updateKinetic, MapMoveReason::MapMoveReason reason) {
        if (durationSeconds > 0) {
            float oldTilt;
            {
                std::lock_guard<std::recursive_mutex> lock(_mutex);
                oldTilt = _viewState.getTilt();
            }
            _animationHandler.setTiltTarget(cameraEvent.isUseDelta() ? oldTilt + cameraEvent.getTiltDelta() : cameraEvent.getTilt(), durationSeconds);
    
            // Animation will start on the next frame
            requestRedraw();
            return;
        }
    
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            
            // Calculate new focusPos, cameraPos and upVec
            cameraEvent.calculate(*_options, _viewState);
        }
    
        // Delay updating the layers, because view state will be updated only after onDrawFrame is called
        viewChanged(true, reason);
    }
    
    void MapRenderer::calculateCameraEvent(CameraZoomEvent& cameraEvent, float durationSeconds, bool updateKinetic, MapMoveReason::MapMoveReason reason) {
        if (durationSeconds > 0) {
            float oldZoom;
            {
                std::lock_guard<std::recursive_mutex> lock(_mutex);
                oldZoom = _viewState.getZoom();
            }
            _animationHandler.setZoomTarget(cameraEvent.isUseDelta() ? oldZoom + cameraEvent.getZoomDelta() : cameraEvent.getZoom(), cameraEvent.isUseTarget() ? &cameraEvent.getTargetPos() : nullptr, durationSeconds);
    
            // Animation will start on the next frame
            requestRedraw();
            return;
        }
    
        MapPos focusPos;
        float deltaZoom;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            float oldZoom = _viewState.getZoom();
            
            // Calculate new focusPos, cameraPos and upVec
            cameraEvent.calculate(*_options, _viewState);
            
            // Calculate parameters for kinetic events
            float zoom = _viewState.getZoom();
            deltaZoom = zoom - oldZoom;

            focusPos = getProjectionSurface()->calculateMapPos(_viewState.getFocusPos());
        }
    
        // Delay updating the layers, because view state will be updated only after onDrawFrame is called
        viewChanged(true, reason);
        
        if (updateKinetic) {
            _kineticEventHandler.setZoomDelta(deltaZoom, cameraEvent.isUseTarget() ? cameraEvent.getTargetPos() : focusPos);
        }
    }
    
    void MapRenderer::moveToFitBounds(const MapBounds& mapBounds, const ScreenBounds& screenBounds, bool integerZoom, bool resetTilt, bool resetRotation, float durationSeconds) {
        CameraPanEvent cameraPanEvent;
        CameraRotationEvent cameraRotationEvent;
        CameraTiltEvent cameraTiltEvent;
        CameraZoomEvent cameraZoomEvent;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            std::shared_ptr<ProjectionSurface> projectionSurface = getProjectionSurface();

            // Find center position
            cglib::vec3<double> centerPos(0, 0, 0);
            {
                cglib::vec3<double> minPos = projectionSurface->calculatePosition(mapBounds.getMin());
                cglib::vec3<double> maxPos = projectionSurface->calculatePosition(mapBounds.getMax());
                cglib::mat4x4<double> transform = projectionSurface->calculateTranslateMatrix(minPos, maxPos, 0.5);
                centerPos = cglib::transform_point(minPos, transform);
                if (std::isnan(cglib::norm(centerPos))) {
                    centerPos = cglib::vec3<double>(0, 0, 0);
                }
            }
            
            // Adjust the camera tilt, rotation and position to the final state of this animation
            cglib::vec3<double> focusPos = centerPos;
            cglib::vec3<double> oldFocusPos = _viewState.getFocusPos();
            cameraPanEvent.setKeepRotation(true);
            cameraPanEvent.setPos(projectionSurface->calculateMapPos(centerPos));
            cameraPanEvent.calculate(*_options, _viewState);
            
            float rotation = 0;
            float oldRotation = _viewState.getRotation();
            if (resetRotation) {
                cameraRotationEvent.setRotation(0);
                cameraRotationEvent.calculate(*_options, _viewState);
            }
    
            float oldTilt = _viewState.getTilt();
            float tilt = 90;
            if (resetTilt) {
                cameraTiltEvent.setKeepRotation(true);
                cameraTiltEvent.setTilt(90);
                cameraTiltEvent.calculate(*_options, _viewState);
            }
            
            // Use binary search to determine what the zoom level of the final state should be, so that all the points
            // would fit in the view
            float oldZoom = _viewState.getZoom();
            MapRange zoomRange(_options->getZoomRange());
            float zoom = _options->getZoomRange().getMin();
            float zoomStep = zoomRange.length() * 0.5f;
            if (mapBounds.getMin() == mapBounds.getMax()) {
                zoom = oldZoom;
                zoomStep = 0;
            }

            // Hack: if view size is zero (view size not known), use given screen bounds for view dimensions
            ViewState viewState(_viewState);
            if (viewState.getWidth() == 0 && viewState.getHeight() == 0) {
                int width = static_cast<int>(screenBounds.getMax().getX() - screenBounds.getMin().getX());
                int height = static_cast<int>(screenBounds.getMax().getY() - screenBounds.getMin().getY());
                Log::Warnf("MapRenderer::moveToFitBounds: Screen size not known yet, using %d, %d", width, height);
                viewState.setScreenSize(width, height);
                viewState.calculateViewState(*_options);
            }

            for (int i = 0; i < 24; i++) {
                cameraZoomEvent.setKeepRotation(true);
                cameraZoomEvent.setZoom(zoom + zoomStep);
                cameraZoomEvent.calculate(*_options, viewState);
                viewState.clampZoom(*_options);

                ScreenPos screenPos = screenBounds.getCenter();
                cglib::vec3<double> pos = viewState.screenToWorld(cglib::vec2<float>(screenPos.getX(), screenPos.getY()), 0, _options);
                if (std::isnan(cglib::norm(pos))) {
                    Log::Error("MapRenderer::moveToFitBounds: Failed to translate screen position!");
                    return;
                }

                cglib::mat4x4<double> transform = projectionSurface->calculateTranslateMatrix(pos, focusPos, 1);
                focusPos = cglib::transform_point(centerPos, transform);
                cameraPanEvent.setPos(projectionSurface->calculateMapPos(focusPos));
                cameraPanEvent.calculate(*_options, viewState);
                viewState.clampFocusPos(*_options);
    
                bool fit = true;
                for (int j = 0; j < 4; j++) {
                    MapPos mapPos(j & 1 ? mapBounds.getMax().getX() : mapBounds.getMin().getX(), j & 2 ? mapBounds.getMax().getY() : mapBounds.getMin().getY());
                    cglib::vec2<float> screenPos = viewState.worldToScreen(projectionSurface->calculatePosition(mapPos), _options);
                    if (!screenBounds.contains(ScreenPos(screenPos(0), screenPos(1)))) {
                        fit = false;
                        break;
                    }
                    cglib::vec3<double> normal = projectionSurface->calculateNormal(mapPos);
                    if (cglib::dot_product(normal, _viewState.getCameraPos() - projectionSurface->calculatePosition(mapPos)) < 0) {
                        fit = false;
                        break;
                    }
                }
                if (fit) {
                    zoom += zoomStep;
                }
                zoomStep /= 2;
            }
            
            if (integerZoom) {
                zoom = (float) std::floor(zoom);
            }
            
            // Reset the camera position, rotation tilt and zoom to the starting state of this animation
            // And then animate them to the final state over time, if needed
            cameraPanEvent.setPos(projectionSurface->calculateMapPos(oldFocusPos));
            cameraPanEvent.calculate(*_options, _viewState);
            cameraPanEvent.setPos(projectionSurface->calculateMapPos(focusPos));
            
            if (resetRotation) {
                cameraRotationEvent.setRotation(oldRotation);
                cameraRotationEvent.calculate(*_options, _viewState);
                cameraRotationEvent.setTargetPos(projectionSurface->calculateMapPos(focusPos));
                cameraRotationEvent.setRotation(rotation);
            }
            
            if (resetTilt) {
                cameraTiltEvent.setTilt(oldTilt);
                cameraTiltEvent.calculate(*_options, _viewState);
                cameraTiltEvent.setTilt(tilt);
            }
            
            cameraZoomEvent.setZoom(oldZoom);
            cameraZoomEvent.calculate(*_options, _viewState);
            cameraZoomEvent.setTargetPos(projectionSurface->calculateMapPos(focusPos));
            cameraZoomEvent.setZoom(zoom);
        }
        
        // Animate the view
        calculateCameraEvent(cameraPanEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
        if (resetRotation) {
            calculateCameraEvent(cameraRotationEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
        }
        if (resetTilt) {
            calculateCameraEvent(cameraTiltEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
        }
        calculateCameraEvent(cameraZoomEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
    
    void MapRenderer::onSurfaceCreated() {
        ThreadUtils::SetThreadPriority(ThreadPriority::MAXIMUM);

        GLContext::LoadExtensions();

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // One-time GL context diagnostics: the depth/stencil resolution and the vertex
        // texture unit count determine which terrain depth model is in effect and how
        // much depth slack it actually has - essential when debugging device-specific
        // terrain occlusion issues (emulator and device configs often differ).
        {
            GLint depthBits = 0, stencilBits = 0, maxVertexTextureUnits = 0;
            glGetIntegerv(GL_DEPTH_BITS, &depthBits);
            glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
            glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxVertexTextureUnits);
            const GLubyte* renderer = glGetString(GL_RENDERER);
            Log::Infof("MapRenderer::onSurfaceCreated: renderer '%s', depth bits %d, stencil bits %d, vertex texture units %d",
                renderer ? reinterpret_cast<const char*>(renderer) : "?", depthBits, stencilBits, maxVertexTextureUnits);
        }

        // If the surface was lost, properly signal about this
        if (_surfaceCreated) {
            onSurfaceDestroyed();
        }
        _surfaceCreated = true;
        _surfaceChanged = true; // should not be needed, do it in any case

        // Reset resource manager
        if (_glResourceManager) {
            _glResourceManager->setGLThreadId(std::thread::id());
        }
        _glResourceManager = std::make_shared<GLResourceManager>();
        _glResourceManager->setGLThreadId(std::this_thread::get_id());

        // Reset screen blending state
        _screenBoundFBOs.clear();
        _screenFrameBuffers.clear();
        _screenBlendShader.reset();

        // Notify renderers about the event
        _backgroundRenderer.onSurfaceCreated(_glResourceManager);
        _skyRenderer.onSurfaceCreated(_glResourceManager);

        GLContext::CheckGLError("MapRenderer::onSurfaceCreated");
    }

    void MapRenderer::onSurfaceChanged(int width, int height) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _viewState.setScreenSize(width, height);
            _viewState.calculateViewState(*_options);
            _viewState.clampZoom(*_options);
            _viewState.clampFocusPos(*_options);
            _screenFrameBuffers.clear(); // reset, as this depends on the surface dimensions
            _surfaceChanged = true;
        }

        DirectorPtr<MapRendererListener> mapRendererListener = _mapRendererListener;
        if (mapRendererListener) {
            mapRendererListener->onSurfaceChanged(width, height);
        }
    }
    
    void MapRenderer::onDrawFrame() {
        if (!_surfaceCreated) {
            Log::Error("MapRenderer::onDrawFrame: Surface not yet created");
            return;
        }

        _redrawPending = false;

        std::vector<std::shared_ptr<OnChangeListener> > onChangeListeners;
        {
            std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
            onChangeListeners = _onChangeListeners;
        }

        DirectorPtr<MapRendererListener> mapRendererListener = _mapRendererListener;

        // Re-set GL thread ids, Windows Phone needs this as onSurfaceCreate/onSurfaceChange may be called from different threads
        _glResourceManager->setGLThreadId(std::this_thread::get_id());

        // Create pending resources
        _glResourceManager->processResources();

        // Check if surface has changed
        if (_surfaceChanged.exchange(false)) {
            int width = 0, height = 0;
            {
                std::lock_guard<std::recursive_mutex> lock(_mutex);
                width = _viewState.getWidth();
                height = _viewState.getHeight();
            }
            glViewport(0, 0, width, height);

            _kineticEventHandler.stopPan();
            _kineticEventHandler.stopRotation();
            _kineticEventHandler.stopZoom();
        
            _lastFrameTime.reset();

            // Perform culling without delay
            viewChanged(false, MapMoveReason::MAP_MOVE_REASON_API);
        }
        
        // Calculate time from the last frame
        std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
        float deltaSeconds = 1.0f / 60.0f;
        if (_lastFrameTime) {
            deltaSeconds = std::max(0.0f, std::chrono::duration_cast<std::chrono::duration<float> >(currentTime - *_lastFrameTime).count());
        }
        _lastFrameTime = currentTime;
    
        // Callback for synchronized rendering
        if (mapRendererListener) {
            mapRendererListener->onBeforeDrawFrame();
        }

        // Calculate camera params and make a synchronized copy of the view state
        ViewState viewState;
        bool terrainDecodeChanged = false;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            terrainDecodeChanged = updateTerrainFlatten(deltaSeconds);

            // Terrain: extend view distances by the terrain height range and keep
            // the camera above the terrain surface.
            std::shared_ptr<ElevationManager> elevationManager;
            if (_options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
                if (auto terrainOptions = _options->getTerrainOptions()) {
                    if (terrainOptions->isEnabled()) {
                        elevationManager = terrainOptions->getElevationManager();
                    }
                }
            }
            if (elevationManager) {
                // The focus sits ON the ground, as in mapbox (transform._centerAltitude): the zoom
                // is the camera's distance to the terrain at the focus, so the ground there keeps
                // the zoom's scale whatever its elevation. The map's projection surface is planar
                // (the terrain one only places vector elements), so every camera event puts the
                // focus at sea level and the ground under it moves as elevation arrives; lift the
                // focus onto it, and the camera with it, whenever they differ. Cached-only, and
                // only when a grid answers: "no data" is not a valley. Without this the ground
                // rose towards a fixed camera (a 270 m hill drew 1.34x closer than asked), and a
                // 3800 m one rose past the camera, which the clearance answered by zooming out to
                // z12.8 for a z16.27 request.
                {
                    const cglib::vec3<double>& focusPos = _viewState.getFocusPos();
                    double terrainZ = 0;
                    if (elevationManager->getDisplayHeightCached(focusPos(0), focusPos(1), terrainZ)) {
                        _viewState.liftFocus(terrainZ - focusPos(2));
                    }
                }
                cglib::vec3<double> cameraPos = _viewState.getCameraPos();
                double minZ = 0, maxZ = 0;
                elevationManager->getDisplayHeightRange(cameraPos(1), minZ, maxZ);
                _viewState.setTerrainHeightRange(static_cast<float>(minZ), static_cast<float>(maxZ));

                // Note: the camera is deliberately NOT clamped above the terrain here.
                // ViewState maintains the invariant dist(camera, focus) == zoom0Distance/2^zoom;
                // mutating the camera position outside of the camera event system breaks it and
                // corrupts the view state. Flying the camera below terrain is a v1 limitation.

                // Refresh vector layers when the elevation data changes (debounced), so that
                // element draw data gets rebuilt with the new heights
                unsigned int elevationVersion = elevationManager->getVersion();
                if (elevationVersion != _layersElevationVersion) {
                    if (!_lastElevationRefreshTime || currentTime - *_lastElevationRefreshTime > std::chrono::milliseconds(ELEVATION_REFRESH_DELAY)) {
                        _layersElevationVersion = elevationVersion;
                        _lastElevationRefreshTime = currentTime;
                        for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
                            if (std::dynamic_pointer_cast<VectorLayer>(layer)) {
                                layer->refresh();
                            }
                        }
                    } else {
                        requestRedraw(); // check again on the next frame
                    }
                }
            } else {
                _viewState.setTerrainHeightRange(0.0f, 0.0f);
            }

            _viewState.calculateViewState(*_options);
            viewState = _viewState;
            _viewState.setHorizontalLayerOffsetDir(0);
        }

        if (terrainDecodeChanged) {
            // The terrain LOD, the overzoom targets and the view distance all differ between the
            // two decode states, so the visible tile set has to be recomputed - the camera has not
            // moved, and nothing else would ask.
            viewChanged(false, MapMoveReason::MAP_MOVE_REASON_API);
        }

        // Calculate map moving animations and kinetic events
        _animationHandler.calculate(viewState, deltaSeconds);
        _kineticEventHandler.calculate(viewState, deltaSeconds);

        // If a post-process effect is set, render the frame into an offscreen buffer
        std::shared_ptr<PostProcessEffect> postProcessEffect = getPostProcessEffect();
        if (postProcessEffect) {
            clearAndBindScreenFBO(_options->getClearColor(), true, false);
        }

        // The style's opinion for this frame, resolved ONCE and before anything draws. The sky and
        // the background plane used to resolve their own fog from an empty environment, so a fog
        // declared by the style reached the tile content and nothing else - hazy ground under a
        // clear sky, and a fog the style switched off still drawn by the sky.
        _frameStyleEnvironment = collectStyleEnvironment(viewState);
        _frameFog = resolveFog(_options->getFogOptions(), _frameStyleEnvironment,
                               resolveLighting(_options->getLightOptions(), _frameStyleEnvironment),
                               viewState.calculateCameraDistance());

        // Render everything
        FRAME_PROF_NOW(profFrameStart);
        FRAME_PROF_RESET();
        FRAME_PROF_GPU_BEGIN(SECTION_SKY);
        initializeRenderState();
        // The shader sky replaces the legacy sky band when it draws.
        bool skyDrawn = _skyRenderer.onDrawFrame(viewState, _frameFog, resolveSky(_options->getSkyOptions(), _frameStyleEnvironment));
        // Timed apart from the sky: both are full-screen-ish draws at the START of the frame, and
        // the first section of a frame also absorbs whatever the GPU idled waiting for the CPU
        // (see GpuFrameProfiler), so one number for the two says nothing about either.
        FRAME_PROF_GPU_BEGIN(SECTION_BACKGROUND);
        // Measurement switch: tangram draws no background geometry at all - their map background
        // is the framebuffer clear colour (core/src/map.cpp) - so this is what that would save.
        //   adb shell setprop debug.massif.background 0
        if (isBackgroundEnabled()) {
            _backgroundRenderer.onDrawFrame(viewState, _frameFog, !skyDrawn);
        }
        FRAME_PROF_ADD(skyMs, profFrameStart);
        drawLayers(deltaSeconds, viewState, static_cast<bool>(postProcessEffect));
        FRAME_PROF_GPU_END();
        FRAME_PROF_END(profFrameStart);
        if (postProcessEffect) {
            // Layers that opted out of the effect are drawn after it resolves, into the same
            // framebuffer and the same depth buffer, and the result is then blitted to the
            // screen. With none of them, the effect writes straight to the screen as before.
            bool overlays = !_overlayLayers.empty();
            applyPostProcessEffect(postProcessEffect, viewState, overlays);
            if (overlays) {
                drawOverlayLayers(deltaSeconds, viewState);
                blendAndUnbindScreenFBO(1.0f);
            }
        }

        // Callback for synchronized rendering
        if (mapRendererListener) {
            mapRendererListener->onAfterDrawFrame();
        }

        // Handle renderer capture callbacks as everything is rendered now
        handleRendererCaptureCallbacks();
        
        // Update billboard placements/visibility
        if (_billboardsChanged.exchange(false)) {
            _billboardPlacementWorker->init(BILLBOARD_PLACEMENT_TASK_DELAY);
        }
        
        // The follow-up frame for the request this one served (see requestRedraw). Taken before the
        // idle test, so the map is not announced idle with a frame still owed.
        if (_redrawExtraFrames.load() > 0) {
            _redrawExtraFrames--;
            DirectorPtr<RedrawRequestListener> redrawRequestListener = _redrawRequestListener;
            if (redrawRequestListener) {
                _redrawPending = true;
                redrawRequestListener->onRedrawRequested();
            }
        }

        // Call listener to inform we are idle now, if no redraw request is pending
        if (!_redrawPending) {
            for (const std::shared_ptr<OnChangeListener>& onChangeListener : onChangeListeners) {
                onChangeListener->onMapIdle();
            }
            _lastFrameTime.reset();
        }

#if MASSIF_VT_RENDER_STATS
        logRenderStats();
#endif

        GLContext::CheckGLError("MapRenderer::onDrawFrame");
    }

#ifdef __ANDROID__
    bool MapRenderer::isBackgroundEnabled() {
        static const bool enabled = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return !(__system_property_get("debug.massif.background", property) > 0 && property[0] == '0');
        }();
        return enabled;
    }
#else
    bool MapRenderer::isBackgroundEnabled() {
        return true;
    }
#endif

    void MapRenderer::onSurfaceDestroyed() {
        // This method may never be called (e.x Android)
        _surfaceCreated = false;

        // Reset resource manager. We tell managers to ignore all resource 'release' operations by invalidating manager thread ids
        if (_glResourceManager) {
            _glResourceManager->setGLThreadId(std::thread::id());
            _glResourceManager.reset();
        }

        // Reset screen blending state
        _screenBoundFBOs.clear();
        _screenFrameBuffers.clear();
        _screenBlendShader.reset();

        // Drop the terrain offscreen targets: their handles belong to the dying context, and a
        // recreated context would otherwise draw into and sample from stale names.
        _terrainDrapeCache.reset();
        _terrainShadowMap.reset();
        _terrainShadowMaskBuffer.reset();
        _groundAOMaskBuffer.reset();
        _groundAODrapeBuffer.reset();
        _labelOcclusionBuffer.reset();
        _shadowMapValid = false;

        // Notify renderers about the event
        _backgroundRenderer.onSurfaceDestroyed();
        _skyRenderer.onSurfaceDestroyed();
    }
    
    void MapRenderer::finishRendering() {
        glFinish();
    }
    
    void MapRenderer::clearAndBindScreenFBO(const Color& color, bool depth, bool stencil) {
        GLint prevBoundFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevBoundFBO);
        GLuint bufferMask = GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0) | (stencil ? GL_STENCIL_BUFFER_BIT : 0);
        _screenBoundFBOs.emplace_back(static_cast<GLuint>(prevBoundFBO), bufferMask);

        std::shared_ptr<FrameBuffer>& frameBuffer = _screenFrameBuffers[bufferMask];
        if (!frameBuffer || !frameBuffer->isValid()) {
            frameBuffer = _glResourceManager->create<FrameBuffer>(_viewState.getWidth(), _viewState.getHeight(), true, depth, stencil);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer->getFBOId());

        glClearColor(color.getR() / 255.0f, color.getG() / 255.0f, color.getB() / 255.0f, color.getA() / 255.0f);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (depth) {
            glDepthMask(GL_TRUE);
        }
        if (stencil) {
            glStencilMask(255);
        }

        glClear(bufferMask);

        if (depth) {
            glDepthMask(GL_FALSE);
        }
        if (stencil) {
            glStencilMask(0);
        }

        GLContext::CheckGLError("MapRenderer::clearAndBindScreenFBO");
    }

    std::shared_ptr<PostProcessEffect> MapRenderer::getPostProcessEffect() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _postProcessEffect;
    }

    void MapRenderer::setPostProcessEffect(const std::shared_ptr<PostProcessEffect>& postProcessEffect) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            if (_postProcessEffect == postProcessEffect) {
                return;
            }
            _postProcessEffect = postProcessEffect;
            _postProcessStartTime = std::chrono::steady_clock::now();
        }
        requestRedraw();
    }

    void MapRenderer::applyPostProcessEffect(const std::shared_ptr<PostProcessEffect>& effect, const ViewState& viewState, bool keepBound) {
        static const GLfloat screenVertices[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };

        if (_screenBoundFBOs.empty()) {
            Log::Error("MapRenderer::applyPostProcessEffect: No bound FBOs");
            return;
        }

        // Optional terrain depth pre-pass (renders into its own FBO and restores the binding)
        GLuint terrainDepthTex = 0;
        if (effect->isTerrainDepthRequired()) {
            std::shared_ptr<TerrainOptions> terrainOptions;
            if (_options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
                terrainOptions = _options->getTerrainOptions();
            }
            if (terrainOptions && terrainOptions->isActive()) {
                if (!_terrainRenderer) {
                    _terrainRenderer = std::make_unique<TerrainRenderer>();
                }
                // Full mesh resolution: an effect drawing lines from this depth would otherwise
                // draw the coarse depth mesh's own triangulation.
                if (_terrainRenderer->renderDepthTexture(viewState, terrainOptions, _glResourceManager, 0)) {
                    terrainDepthTex = _terrainRenderer->getDepthTextureId();
                }
            }
        }

        GLuint prevBoundFBO = _screenBoundFBOs.back().first;
        GLuint bufferMask = _screenBoundFBOs.back().second;

        std::shared_ptr<FrameBuffer>& frameBuffer = _screenFrameBuffers[bufferMask];
        if (!frameBuffer || !frameBuffer->isValid()) {
            _screenBoundFBOs.pop_back();
            return; // should not happen, just safety
        }

        GLuint sourceTexId = frameBuffer->getColorTexId();
        if (keepBound) {
            // Layers that opted out of post-processing still have to be drawn, and they have to
            // be depth-tested against the terrain the effect just stylized. So the effect writes
            // into the framebuffer's SECOND color texture instead of the screen: same FBO, same
            // depth buffer, and the caller keeps drawing into it before blitting it out.
            frameBuffer->attachSecondaryColorTex(true);
            _postProcessSecondaryActive = true;
        } else {
            _screenBoundFBOs.pop_back();
            if (bufferMask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
                frameBuffer->discard(false, (bufferMask & GL_DEPTH_BUFFER_BIT) != 0, (bufferMask & GL_STENCIL_BUFFER_BIT) != 0);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, prevBoundFBO);
        }

        // Compile the effect shader on demand
        if (!_postProcessShader || !_postProcessShader->isValid() || _postProcessShaderName != effect->getName()) {
            _postProcessShader = _glResourceManager->create<Shader>("postprocess_" + effect->getName(), POST_PROCESS_VERTEX_SHADER, effect->getFragmentShader());
            _postProcessShaderName = effect->getName();
        }
        if (!_postProcessShader) {
            return;
        }

        glDisable(GL_BLEND);
        // The pass covers the screen and must neither test nor write depth: the depth buffer is
        // the terrain's, and the layers drawn after the effect still need it.
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        GLuint progId = _postProcessShader->getProgId();
        glUseProgram(progId);

        glVertexAttribPointer(_postProcessShader->getAttribLoc("a_coord"), 2, GL_FLOAT, GL_FALSE, 0, screenVertices);
        glEnableVertexAttribArray(_postProcessShader->getAttribLoc("a_coord"));

        // Effects declare only the uniforms they use, so query the locations directly
        GLint loc = glGetUniformLocation(progId, "uColorTex");
        if (loc >= 0) {
            glUniform1i(loc, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sourceTexId);
        if (terrainDepthTex != 0 && (loc = glGetUniformLocation(progId, "uTerrainDepthTex")) >= 0) {
            glUniform1i(loc, 1);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, terrainDepthTex);
            glActiveTexture(GL_TEXTURE0);
        }

        if ((loc = glGetUniformLocation(progId, "uInvScreenSize")) >= 0) {
            glUniform2f(loc, 1.0f / _viewState.getWidth(), 1.0f / _viewState.getHeight());
        }
        if ((loc = glGetUniformLocation(progId, "uNear")) >= 0) {
            glUniform1f(loc, viewState.getNear());
        }
        if ((loc = glGetUniformLocation(progId, "uFar")) >= 0) {
            glUniform1f(loc, viewState.getFar());
        }
        if ((loc = glGetUniformLocation(progId, "uProjInvScale")) >= 0) {
            float tanHalfFOVY = static_cast<float>(viewState.getTanHalfFOVY());
            glUniform2f(loc, tanHalfFOVY * viewState.getAspectRatio(), tanHalfFOVY);
        }
        if ((loc = glGetUniformLocation(progId, "uTime")) >= 0) {
            float time = 0;
            if (_postProcessStartTime) {
                time = std::chrono::duration_cast<std::chrono::duration<float> >(std::chrono::steady_clock::now() - *_postProcessStartTime).count();
            }
            glUniform1f(loc, time);
        }

        for (const auto& param : effect->getFloatParameters()) {
            if ((loc = glGetUniformLocation(progId, param.first.c_str())) >= 0) {
                glUniform1f(loc, param.second);
            }
        }
        for (const auto& param : effect->getColorParameters()) {
            if ((loc = glGetUniformLocation(progId, param.first.c_str())) >= 0) {
                glUniform4f(loc, param.second.getR() / 255.0f, param.second.getG() / 255.0f, param.second.getB() / 255.0f, param.second.getA() / 255.0f);
            }
        }

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisableVertexAttribArray(_postProcessShader->getAttribLoc("a_coord"));
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        GLContext::CheckGLError("MapRenderer::applyPostProcessEffect");
    }

    void MapRenderer::blendAndUnbindScreenFBO(float opacity) {
        static const GLfloat screenVertices[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };

        if (_screenBoundFBOs.empty()) {
            Log::Error("MapRenderer::blendAndUnbindScreenFBO: No bound FBOs");
            return;
        }

        GLuint prevBoundFBO = _screenBoundFBOs.back().first;
        GLuint bufferMask = _screenBoundFBOs.back().second;
        _screenBoundFBOs.pop_back();
        
        std::shared_ptr<FrameBuffer>& frameBuffer = _screenFrameBuffers[bufferMask];
        if (!frameBuffer || !frameBuffer->isValid()) {
            return; // should not happen, just safety
        }
        if (bufferMask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
            frameBuffer->discard(false, (bufferMask & GL_DEPTH_BUFFER_BIT) != 0, (bufferMask & GL_STENCIL_BUFFER_BIT) != 0);
        }
        // Normally the framebuffer's own color texture goes out (a layer rendering itself at
        // partial opacity). The one case where it is not is the outermost unwind of a
        // post-process effect that resolved into the secondary texture and let overlay layers
        // draw on top of it - then that texture is the frame, and the primary one is attached
        // again for the next frame to be drawn into.
        GLuint colorTexId = frameBuffer->getColorTexId();
        if (_postProcessSecondaryActive && _screenBoundFBOs.empty()) {
            colorTexId = frameBuffer->getAttachedColorTexId();
            frameBuffer->attachSecondaryColorTex(false);
            _postProcessSecondaryActive = false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, prevBoundFBO);

        if (!_screenBlendShader || !_screenBlendShader->isValid()) {
            _screenBlendShader = _glResourceManager->create<Shader>("blend", BLEND_VERTEX_SHADER, BLEND_FRAGMENT_SHADER);
        }
        
        glUseProgram(_screenBlendShader->getProgId());

        // The blit covers the screen and must not be depth-tested: the depth buffer it would test
        // against belongs to the SCREEN framebuffer, which nothing clears (the scene is drawn into
        // an offscreen one). The first blit passed and wrote its own depth, every later one failed
        // against it - the map rendered at full speed while the window kept the frame from before
        // the effect was attached.
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        glVertexAttribPointer(_screenBlendShader->getAttribLoc("a_coord"), 2, GL_FLOAT, GL_FALSE, 0, screenVertices);
        glEnableVertexAttribArray(_screenBlendShader->getAttribLoc("a_coord"));
        
        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::identity();
        glUniformMatrix4fv(_screenBlendShader->getUniformLoc("u_mvpMat"), 1, GL_FALSE, mvpMatrix.data());
        
        glUniform1i(_screenBlendShader->getUniformLoc("u_tex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, colorTexId);

        glUniform4f(_screenBlendShader->getUniformLoc("u_color"), opacity, opacity, opacity, opacity);
        glUniform2f(_screenBlendShader->getUniformLoc("u_invScreenSize"), 1.0f / _viewState.getWidth(), 1.0f / _viewState.getHeight());
        
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        glBindTexture(GL_TEXTURE_2D, 0);
        
        glDisableVertexAttribArray(_screenBlendShader->getAttribLoc("a_coord"));
        glEnable(GL_DEPTH_TEST);

        GLContext::CheckGLError("MapRenderer::blendAndUnbindScreenFBO");
    }

    void MapRenderer::drawMaskQuad(unsigned int texture, float invWidth, float invHeight) {
        if (texture == 0 || !_glResourceManager) {
            return;
        }
        static const GLfloat screenVertices[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };

        if (!_screenBlendShader || !_screenBlendShader->isValid()) {
            _screenBlendShader = _glResourceManager->create<Shader>("blend", BLEND_VERTEX_SHADER, BLEND_FRAGMENT_SHADER);
        }
        glUseProgram(_screenBlendShader->getProgId());

        glVertexAttribPointer(_screenBlendShader->getAttribLoc("a_coord"), 2, GL_FLOAT, GL_FALSE, 0, screenVertices);
        glEnableVertexAttribArray(_screenBlendShader->getAttribLoc("a_coord"));

        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::identity();
        glUniformMatrix4fv(_screenBlendShader->getUniformLoc("u_mvpMat"), 1, GL_FALSE, mvpMatrix.data());
        glUniform1i(_screenBlendShader->getUniformLoc("u_tex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform4f(_screenBlendShader->getUniformLoc("u_color"), 1.0f, 1.0f, 1.0f, 1.0f);
        glUniform2f(_screenBlendShader->getUniformLoc("u_invScreenSize"), invWidth, invHeight);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisableVertexAttribArray(_screenBlendShader->getAttribLoc("a_coord"));

        GLContext::CheckGLError("MapRenderer::drawMaskQuad");
    }

    void MapRenderer::multiplyScreenMask(unsigned int texture, float invWidth, float invHeight) {
        // dst *= mask. No depth test: the mask holds white everywhere no footprint reaches, so the
        // multiply is a no-op there and only the ground around a building is touched. The strip's
        // two triangles wind opposite ways, so culling would discard exactly half of it.
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        drawMaskQuad(texture, invWidth, invHeight);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    void MapRenderer::setZBuffering(bool enable) {
        glDepthMask(enable ? GL_TRUE : GL_FALSE);
    }

    void MapRenderer::calculateRayIntersectedElements(const MapPos& targetPos, ViewState& viewState, std::vector<RayIntersectedElement>& results) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            viewState = _viewState;
        }
        if (!viewState.getProjectionSurface()) {
            return;
        }

        cglib::vec3<double> origin = viewState.getCameraPos();
        cglib::vec3<double> target = viewState.getProjectionSurface()->calculatePosition(targetPos);
        cglib::ray3<double> ray(origin, target - origin);
        calculateRayIntersectedElements(ray, viewState, results);
    }

    void MapRenderer::calculateRayIntersectedElements(const cglib::ray3<double>& ray, ViewState& viewState, std::vector<RayIntersectedElement>& results) {
        // Normal layer click detection is done in the layer order
        for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
            layer->calculateRayIntersectedElements(ray, viewState, results);
        }
    }
     
    void MapRenderer::billboardsChanged() {
        _billboardsChanged = true;
    }

    double MapRenderer::calculateTerrainParallax(const std::shared_ptr<TerrainOptions>& terrainOptions) const {
        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
        if (!elevationManager) {
            return 0;
        }
        // At the APP's exaggeration, not the ramped one: the ramp is what this decides.
        double minZ = 0, maxZ = 0;
        elevationManager->getDisplayHeightRange(_viewState.getCameraPos()(1), terrainOptions->getExaggeration(), minZ, maxZ);
        // NO DATA, and PARTIAL data, are not FLAT. A view whose DEM is still arriving reports a
        // small height range, which reads as small parallax and flattens the map - and flattening
        // stops the elevation decode, so the range never grows and 3D never comes back. Whether
        // terrain appeared at all then depended on which tiles happened to land first, which is why
        // the same launch gave 3D or 2D at random. Unknown means "do not flatten yet": the rule is
        // re-evaluated every frame and answers properly once the data settles.
        if (!(maxZ > minZ) || !_autoFlattenSeenTerrain || _autoFlattenDataQuiet < TERRAIN_SWITCH_WARM_TIMEOUT) {
            return std::numeric_limits<double>::infinity();
        }
        double halfWidth = _viewState.getHalfWidth(), halfHeight = _viewState.getHalfHeight();
        return AutoFlatten::parallax(std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight), maxZ - minZ, _viewState.calculateCameraDistance());
    }

    bool MapRenderer::updateTerrainFlatten(float deltaSeconds) {
        std::shared_ptr<TerrainOptions> terrainOptions;
        if (_options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
            terrainOptions = _options->getTerrainOptions();
        }
        if (!terrainOptions || !terrainOptions->isEnabled()) {
            return false;
        }

        // Terrain reached at least once: only then may the rule flatten. See the member.
        _autoFlattenSeenTerrain = _autoFlattenSeenTerrain || _flattenSwitchState.phase == FlattenSwitch::Phase::TERRAIN;

        // How long the elevation data has been still. Every DEM tile that lands bumps the data
        // version, so this is exactly "nothing new has arrived recently" - see
        // calculateTerrainParallax, which will not decide before it.
        if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
            unsigned int dataVersion = elevationManager->getDataVersion();
            if (dataVersion != _autoFlattenDataVersion) {
                _autoFlattenDataVersion = dataVersion;
                _autoFlattenDataQuiet = 0.0f;
                requestRedraw(); // nothing else asks for the frame the wait ends on
            } else {
                _autoFlattenDataQuiet += deltaSeconds;
            }
        }

        // Not while the app is driving the ratio itself: the rule would take it straight back.
        bool manual = terrainOptions->isManualFlatten();
        float parallaxThreshold = terrainOptions->getAutoFlattenParallax();
        float tiltThreshold = terrainOptions->getAutoFlattenTilt();
        if (!manual && (parallaxThreshold > 0 || tiltThreshold > 0)) {
            // The parallax costs a height-range lookup, so only pay for it when it is part of the rule.
            double parallax = parallaxThreshold > 0 ? calculateTerrainParallax(terrainOptions) : 0;
            bool flatten = AutoFlatten::shouldFlatten(parallax, parallaxThreshold, _viewState.getTilt(), tiltThreshold, terrainOptions->isFlattened());
            // Only on a CHANGE of the rule's own answer - see AutoFlatten::Trigger.
            if (_autoFlattenTrigger.changed(flatten)) {
                Log::Infof("MapRenderer: auto-flatten %s (parallax %.1f px vs %.1f, tilt %.1f vs %.1f, data quiet %.1f s, seen terrain %d)",
                    flatten ? "ON" : "off", parallax, parallaxThreshold, _viewState.getTilt(), tiltThreshold, _autoFlattenDataQuiet, _autoFlattenSeenTerrain ? 1 : 0);
                terrainOptions->setFlattened(flatten);
                manual = terrainOptions->isManualFlatten(); // setFlattened hands the ratio back
            }
        } else {
            // The rule turned off while its last answer was ON: hand the state back, or the map
            // stays flat for good. Seen at startup: the SDK's defaults (2 px / 88 degrees) ran on
            // the first frame, at the default tilt of 90, before the app set its own thresholds to
            // 0 - and with the rule off nothing ever asked for 3D again. Only what the rule itself
            // set is released; an app's explicit setFlattened(true) is not the rule's to undo.
            if (_autoFlattenTrigger.last == 1 && !manual && terrainOptions->isFlattened()) {
                Log::Info("MapRenderer: auto-flatten disabled while ON - releasing the flat state it set");
                terrainOptions->setFlattened(false);
            }
            _autoFlattenTrigger = AutoFlatten::Trigger(); // re-arm: the next answer is an edge again
        }

        // Seed from what the app (or the rule just above, on the very first frame) asked for, so a
        // map starting in 2D neither animates down from a 3D it never showed nor decodes for it.
        if (_flattenSwitchOptions.lock() != terrainOptions) {
            _flattenSwitchOptions = terrainOptions;
            _autoFlattenTrigger = AutoFlatten::Trigger();
            _flattenSwitchState = FlattenSwitch::State();
            _flattenSwitchState.ratio = terrainOptions->getFlattenRatio();
            _flattenSwitchState.decode3D = terrainOptions->isDecodeActive();
            _flattenSwitchState.phase = _flattenSwitchState.ratio >= 1.0f ? FlattenSwitch::Phase::FLAT
                                      : _flattenSwitchState.ratio <= 0.0f ? FlattenSwitch::Phase::TERRAIN
                                                                          : FlattenSwitch::Phase::RAMPING;
            terrainOptions->applyFlattenRatio(_flattenSwitchState.ratio); // hands the state over: from here setFlattened only asks
            Log::Infof("MapRenderer: terrain switch seeded - ratio %.2f, decode3D %d, phase %d, flattened %d, manual %d",
                _flattenSwitchState.ratio, _flattenSwitchState.decode3D ? 1 : 0, static_cast<int>(_flattenSwitchState.phase), terrainOptions->isFlattened() ? 1 : 0, manual ? 1 : 0);
        }

        FlattenSwitch::Input input;
        input.flatten = terrainOptions->isFlattened();
        input.manual = manual;
        input.manualRatio = terrainOptions->getManualFlattenRatio();
        input.fullSwitch = terrainOptions->getFlattenMode() == TerrainFlattenMode::TERRAIN_FLATTEN_MODE_FULL;
        input.deltaSeconds = deltaSeconds;
        input.flattenDuration = terrainOptions->getAutoFlattenDuration();
        float riseDuration = terrainOptions->getAutoFlattenRiseDuration();
        input.riseDuration = riseDuration < 0 ? input.flattenDuration : riseDuration;
        input.warmTimeout = TERRAIN_SWITCH_WARM_TIMEOUT;
        // The tile gate, for both the automatic wait and an app-driven rise.
        if (FlattenSwitch::isWaitingForTiles(_flattenSwitchState, input)) {
            input.tilesReady = true;
            for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
                if (auto tileLayer = std::dynamic_pointer_cast<TileLayer>(layer)) {
                    input.tilesReady = tileLayer->isTerrainDecodeSettled() && input.tilesReady;
                }
            }
            requestRedraw(); // nothing else asks for the frame the wait ends on
        }

        FlattenSwitch::State next = FlattenSwitch::step(_flattenSwitchState, input);
        bool decodeChanged = next.decode3D != _flattenSwitchState.decode3D;
        bool ratioChanged = next.ratio != _flattenSwitchState.ratio;
        if (next.phase != _flattenSwitchState.phase) {
            // The 2D/3D switch's phase is rare and is the whole story of "why is this map flat".
            Log::Infof("MapRenderer: terrain switch phase %d -> %d (ratio %.2f -> %.2f, flatten asked %d, manual %d, tiles ready %d, warm %.1f s)",
                static_cast<int>(_flattenSwitchState.phase), static_cast<int>(next.phase), _flattenSwitchState.ratio, next.ratio,
                input.flatten ? 1 : 0, input.manual ? 1 : 0, input.tilesReady ? 1 : 0, next.warmSeconds);
        }
        _flattenSwitchState = next;
        terrainOptions->setSwitching(FlattenSwitch::isWaitingForTiles(next, input));
        if (!decodeChanged && !ratioChanged) {
            return false;
        }
        if (ratioChanged) {
            terrainOptions->applyFlattenRatio(next.ratio);
        }
        if (decodeChanged) {
            terrainOptions->setDecodeActive(next.decode3D);
        }
        requestRedraw();
        // The tile set differs between the two decode states (the terrain LOD, the overzoom
        // targets, the view distance), and the camera has not moved, so nothing else would ask.
        return decodeChanged;
    }

    void MapRenderer::vtLabelsChanged(const std::shared_ptr<Layer>& layer, bool delay) {
        _vtLabelPlacementWorker->init(layer, delay ? VT_LABEL_PLACEMENT_TASK_DELAY : 0);
    }
    
    void MapRenderer::layerChanged(const std::shared_ptr<Layer>& layer, bool delay) {
        // If screen size has been set, load the layers, otherwise wait for the onSurfaceChanged method
        // which will also start the cull worker
        if (_surfaceCreated) {
            int delayTime = layer->getCullDelay();
            _cullWorker->init(layer, delay ? delayTime : 0);
        }
    }
    
    void MapRenderer::viewChanged(bool delay, MapMoveReason::MapMoveReason reason) {
        std::shared_ptr<Layer> vectorTileLayer;
        for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
            int delayTime = layer->getCullDelay();
            _cullWorker->init(layer, delay ? delayTime : 0);
            if (!vectorTileLayer && std::dynamic_pointer_cast<VectorTileLayer>(layer)) {
                vectorTileLayer = layer;
            }
        }

        // A placement pass is otherwise only asked for when the tile set changes, but a label's
        // envelope is screen space: zooming in makes room nothing would notice, and a name that fell
        // back to its icon (shield-text-optional) would keep it. Postponed rather than queued, so a
        // zoom gesture places once when it ends instead of thrashing at every step.
        if (vectorTileLayer) {
            float zoom = getViewState().getZoom();
            if (std::abs(zoom - _lastLabelPlacementZoom) >= LABEL_PLACEMENT_ZOOM_THRESHOLD) {
                _lastLabelPlacementZoom = zoom;
                _vtLabelPlacementWorker->postpone(vectorTileLayer, LABEL_PLACEMENT_ZOOM_DELAY);
            }
        }
    
        billboardsChanged();
    
        std::vector<std::shared_ptr<OnChangeListener> > onChangeListeners;
        {
            std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
            onChangeListeners = _onChangeListeners;
        }
        for (const std::shared_ptr<OnChangeListener>& onChangeListener : onChangeListeners) {
            onChangeListener->onMapChanged(reason);
        }
        
        requestRedraw();
    }
    
    void MapRenderer::registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.push_back(listener);
    }

    void MapRenderer::unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.erase(std::remove(_onChangeListeners.begin(), _onChangeListeners.end(), listener), _onChangeListeners.end());
    }

    void MapRenderer::initializeRenderState() const {
        // Enable backface culling
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    
        // Enable blending, use premultiplied alpha
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    
        // Disable dithering for better performance
        glDisable(GL_DITHER);
    
        // Enable depth testing, disable writing, set up clear color, etc
        Color clearColor = _options->getClearColor();
        glClearColor(clearColor.getR() / 255.0f, clearColor.getG() / 255.0f, clearColor.getB() / 255.0f, clearColor.getA() / 255.0f);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(255);
    
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glDepthMask(GL_FALSE);
        glStencilMask(0);
    }
    
    // A cached shadow map is refreshed at least this often anyway: elevation tiles can stream in
    // without changing the light box or the caster tile list, and a shadow cast by data that has
    // since arrived would otherwise never appear.
    // Screen divisor for the terrain shadow mask. A terrain shadow edge is a penumbra, so a
    // quarter of the screen resolution is not visible in the result - measured against a half:
    // the mask pass 14-16 ms -> 8-9 ms, and 8.5 -> 9.6 fps.
    static const int SHADOW_MASK_DIVISOR = 4;
    // ... and for the extrusions' contact shadows. Half, not a quarter: this one is a few metres
    // wide on the ground, so its own gradient is most of what a quarter-resolution texel would
    // average away. The LINEAR fetch that reads it back is also the only blur the effect gets.
    static const int GROUND_AO_MASK_DIVISOR = 2;
    // Half resolution: the buffer answers one depth comparison per label anchor, over a square of
    // several pixels, so its own texels are never seen. mapbox samples a 30 px square.
    static const int LABEL_OCCLUSION_DIVISOR = 2;
    static const float LABEL_OCCLUSION_SIZE_PIXELS = 30.0f;
    // The tile zoom the caster ring's reach is derived from: coarse enough that one tile spans the
    // massif whose shadow reaches the view, not just the ground under it. ~28 km at latitude 45.
    static const int SHADOW_RELIEF_ZOOM = 10;
    // Ceiling on the caster set. The ring's quadtree subdivision is 4^(maxCoverZoom - ringZoom),
    // so a coarse ring against a deep cover is unbounded; this is what keeps it a frame cost
    // rather than an OOM.
    static const std::size_t MAX_SHADOW_CASTER_TILES = 2048;
    static const int SHADOW_MAP_MAX_AGE = 30;
    // Frames between two refreshes driven by newly arrived tile content.
    static const int SHADOW_MAP_CONTENT_INTERVAL = 4;
    // How far the extrusions may grow before the map is redrawn, in units of one tile's full
    // height: about a dozen refreshes over a whole fade, whatever the frame rate, instead of one
    // per frame.
    static const float SHADOW_MAP_FADE_STEP = 0.08f;

    // Caster-pass counters, cumulative since start: compared with the frame count they say how
    // much of the shadow cost the map cache is saving. File scope because the pass itself and the
    // periodic dump that prints them now live in different functions.
    static int shadowPasses = 0;
    static int shadowCasterDraws = 0;
    static int shadowExtrusionDraws = 0;
    static int shadowCastersNoElevation = 0;
    static double shadowMsSum = 0;

    void MapRenderer::applyTerrainShadows(const std::vector<std::shared_ptr<TileLayer> >& tileLayers, const std::vector<vt::TileId>& coverTileIds, const std::shared_ptr<TerrainOptions>& terrainOptions, const ViewState& viewState, int prevFBO, bool contentChanged, bool castShadows, ResolvedLighting& lighting, std::array<double, TerrainShadowMap::MAX_CASCADES>& shadowTexelMeters) {
        // Directional shadows. The caster pass draws exactly the terrain surfaces
        // that are about to be drawn on screen, from the sun, into a packed-depth
        // texture; the surface shader then looks itself up in it. Casters and
        // receivers share one vertex shader and one elevation fetch, so the shadow
        // geometry cannot disagree with the rendered geometry.
        float shadowStrength = 0.0f;
        unsigned int shadowTexture = 0;
        int shadowMapSize = 0, shadowCascades = 1;
        float shadowSoftness = 1.0f;
        
        // mapbox's u_shadow_bias (3d-style/render/shadow_renderer.ts): constant, slope scale,
        // slope CAP, in normalised light depth and shared by every cascade. LightOptions'
        // ShadowBias scales the triple, so 1 is theirs exactly.
        cglib::vec3<float> shadowBias(0.0f, 0.0f, 0.0f);
        std::array<float, TerrainShadowMap::MAX_CASCADES> shadowDepthScales = { };
        std::array<cglib::mat4x4<double>, TerrainShadowMap::MAX_CASCADES> lightViewProjs;
        lightViewProjs.fill(cglib::mat4x4<double>::identity());
        // The styles get a say in every light and shadow property; whatever they do
        // not mention stays with LightOptions. Collected once for the frame (see
        // collectStyleEnvironment) and re-read every frame, so it may follow the zoom.
        lighting = resolveLighting(_options->getLightOptions(), _frameStyleEnvironment);
        // Floor the sun altitude for the SHADOW pass alone: a lower sun stretches the light box
        // past the drawn cover and the cascades go coarse (docs/internals/rendering/08-lighting-sky-fog.md).
        cglib::vec3<float> shadowSunDir = lighting.sunDir;
        {
            static const float MIN_SHADOW_SUN_SIN = 0.2588f; // sin(15 degrees)
            if (shadowSunDir(2) < MIN_SHADOW_SUN_SIN) {
                float horizontal = std::sqrt(shadowSunDir(0) * shadowSunDir(0) + shadowSunDir(1) * shadowSunDir(1));
                float scale = std::sqrt(std::max(0.0f, 1.0f - MIN_SHADOW_SUN_SIN * MIN_SHADOW_SUN_SIN));
                if (horizontal > 1.0e-6f) {
                    // Keep the azimuth: only the altitude is raised, so the shadows
                    // still fall in the direction the sun says, just shorter.
                    shadowSunDir(0) *= scale / horizontal;
                    shadowSunDir(1) *= scale / horizontal;
                }
                shadowSunDir(2) = MIN_SHADOW_SUN_SIN;
            }
        }
        // mapbox's constants, verbatim. The pair they ship depends on whether the normal offset
        // is on, because that offset already moves the sample off the surface and the constant
        // then has far less to cover.
        {
            // mapbox's SHAPE with our units: a constant, a term growing as the surface turns away
            // from the light, and a cap on it - in METRES, because our light box normalises a depth
            // of tens of thousands of km and a fraction of that is not a shadow bias but a shrug.
            // The normal offset already moves the sample off the surface, so the constant is
            // halved when it is on, as theirs is.
            float scale = std::max(0.0f, lighting.shadowBias);
            float constant = (lighting.shadowNormalOffset > 0.0f ? 1.0f : 2.0f);
            shadowBias = cglib::vec3<float>(constant, 0.25f, 4.0f) * scale;
        }
        bool shadowsWanted = false;
        {
            // Not while the 2D/3D switch is ramping. A cascade is only redrawn when its light box
            // or its caster list changes (below), and neither does during the ramp - while the
            // ground RECEIVING the shadow is displaced every frame, so the map wears a shadow of a
            // terrain it no longer has. Dropped for the ramp rather than re-cast every frame: that
            // is a full caster pass on the frames least able to afford one.
            bool switching = terrainOptions->getFlattenRatio() > 0.0f;
            shadowsWanted = castShadows && !switching && lighting.terrainLightingEnabled && lighting.shadowStrength > 0.0f && !coverTileIds.empty();
            if (shadowsWanted) {
                if (!_terrainShadowMap) {
                    _terrainShadowMap = std::make_unique<TerrainShadowMap>();
                }
                _terrainShadowMap->setSize(lighting.shadowMapSize, lighting.shadowCascades);
                // Fit the light box to the elevation the shadowed ground actually
                // spans, plus headroom for what stands on it. With a low sun the box
                // is stretched by this range divided by tan(altitude), so a generous
                // slab is the difference between half-metre and ten-metre texels.
                double minHeight = 0, maxHeight = 0;
                // Per tile as well as overall: a cascade covering a small piece of
                // ground can then fit its box to THAT piece's relief instead of to
                // the whole scene's, which at a low sun is what sets the box size.
                std::vector<std::pair<double, double> > tileHeights;
                if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                    bool first = true;
                    tileHeights.reserve(coverTileIds.size());
                    // Only tiles that REALLY have elevation shape the slab. getMinMaxDisplayHeight
                    // falls back to 0..maxSeenElevation - the highest ground anywhere in the session
                    // - for a tile whose DEM has not arrived, and getMinMaxDisplayHeightExact throws
                    // that flag away because it returns void. One unloaded tile therefore stretched
                    // the slab to a massif visited earlier: measured over flat Paris, after the
                    // Matterhorn example, a slab of ~121 km and a light box of 30,000 km - a depth
                    // map far too coarse to separate a building from the ground under it.
                    //
                    // tileHeights still gets an entry PER COVER TILE: vt narrows each cascade's slab
                    // with it and only trusts it when the two lists are the same length. A tile with
                    // no data contributes the overall range instead, which cannot narrow wrongly.
                    std::vector<bool> tileKnown;
                    tileKnown.reserve(coverTileIds.size());
                    tileHeights.assign(coverTileIds.size(), std::make_pair(0.0, 0.0));
                    for (std::size_t i = 0; i < coverTileIds.size(); i++) {
                        const vt::TileId& tileId = coverTileIds[i];
                        double tileMin = 0, tileMax = 0;
                        bool known = elevationManager->getMinMaxDisplayHeightCached(MapTile(tileId.x, tileId.y, tileId.zoom, 0), tileMin, tileMax);
                        tileKnown.push_back(known);
                        if (!known) {
                            continue;
                        }
                        double tileHeadroom = std::max(1.0e-5, (tileMax - tileMin) * 0.25);
                        tileHeights[i] = std::make_pair(tileMin - tileHeadroom, tileMax + tileHeadroom);
                        if (first) {
                            minHeight = tileMin;
                            maxHeight = tileMax;
                            first = false;
                        } else {
                            minHeight = std::min(minHeight, tileMin);
                            maxHeight = std::max(maxHeight, tileMax);
                        }
                    }
                    for (std::size_t i = 0; i < tileKnown.size(); i++) {
                        if (!tileKnown[i]) {
                            tileHeights[i] = std::make_pair(minHeight, maxHeight);
                        }
                    }
                    if (!first) {
                        double headroom = std::max(1.0e-5, (maxHeight - minHeight) * 0.25);
                        minHeight -= headroom;
                        maxHeight += headroom;
                    }
                }
                // Casters reach beyond the visible tiles: a mountain just off screen
                // still throws its shadow into the view, and without this its shadow
                // vanishes as you zoom in and it leaves the visible set.
                std::vector<vt::TileId> casterTileIds = coverTileIds;
                int casterMargin = lighting.shadowCasterMargin;
                if (casterMargin > 0) {
                    // The caster set must stay a PARTITION of the ground, like the cover it extends.
                    // A margin ring is generated at its own cover tile's zoom and the cover mixes
                    // zooms, so the ring around a coarse cover tile lands on top of the fine tiles
                    // next to it. Two casters over the same ground at different DEM levels disagree
                    // by tens of metres, the shallower one wins the depth test, and the receiver -
                    // which uses the fine level - is then in the shadow of its own ground: blocky
                    // patches that grow with the tilt, because a tilted cover mixes more zooms.
                    // An overlapping candidate is SUBDIVIDED rather than dropped, so the ground it
                    // covers outside the finer tiles keeps a caster.
                    using TileKey = std::pair<int, std::pair<int, int> >;
                    auto keyOf = [](const vt::TileId& tileId) { return TileKey(tileId.zoom, { tileId.x, tileId.y }); };
                    std::set<TileKey> taken, takenAncestors;
                    int maxCoverZoom = 0;
                    auto take = [&](const vt::TileId& tileId) {
                        taken.insert(keyOf(tileId));
                        for (int zoom = tileId.zoom - 1; zoom >= 0; zoom--) {
                            int shift = tileId.zoom - zoom;
                            takenAncestors.insert(TileKey(zoom, { tileId.x >> shift, tileId.y >> shift }));
                        }
                    };
                    for (const vt::TileId& tileId : coverTileIds) {
                        take(tileId);
                        maxCoverZoom = std::max(maxCoverZoom, tileId.zoom);
                    }
                    // The ring is bounded by how far a shadow can be THROWN, not by a fixed number
                    // of tiles: relief / tan(sun altitude), which the 15-degree floor caps at about
                    // 3.7 x the relief. A ring counted in tiles is a distance that shrinks with the
                    // zoom - at z16 a tile is ~430 m, so three of them reach 1.3 km and a mountain
                    // 5 km away simply has no caster, which is a mountain shadow that appears only
                    // when you zoom out far enough to pull it into the cover.
                    //
                    // Holding the DISTANCE means dropping the RESOLUTION, or the count explodes
                    // (7 km at z16 is a 35x35 ring). The ring is generated at the coarsest zoom
                    // that still needs no more than casterMargin tiles to span the throw, and the
                    // partition logic below subdivides whatever overlaps the finer cover.
                    // The relief that matters is the terrain AROUND the view, not the cover's own.
                    // At z16 over a valley floor the cover is a few tiles of flat ground - metres of
                    // relief, a throw of a couple of hundred metres, and a ring that collapses back
                    // onto the cover's own zoom. The mountain casting into that view is outside the
                    // cover entirely, so its height was never in the range. A coarse ancestor spans
                    // the massif and costs one elevation query.
                    double relief = std::max(0.0, maxHeight - minHeight);
                    if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                        const vt::TileId& sample = coverTileIds[coverTileIds.size() / 2];
                        int coarseZoom = std::min(sample.zoom, SHADOW_RELIEF_ZOOM);
                        int shift = sample.zoom - coarseZoom;
                        double coarseMin = 0, coarseMax = 0;
                        if (elevationManager->getMinMaxDisplayHeightCached(MapTile(sample.x >> shift, sample.y >> shift, coarseZoom, 0), coarseMin, coarseMax)) {
                            relief = std::max(relief, coarseMax - coarseMin);
                        }
                    }
                    double sunUp = std::max(0.05f, shadowSunDir(2));
                    double throwDistance = relief * std::sqrt(std::max(0.0, 1.0 - sunUp * sunUp)) / sunUp;
                    int ringZoom = maxCoverZoom;
                    if (throwDistance > 0) {
                        // 2^z <= casterMargin * WORLD_SIZE / throw
                        double limit = casterMargin * Const::WORLD_SIZE / throwDistance;
                        if (limit > 1) {
                            ringZoom = std::min(maxCoverZoom, static_cast<int>(std::floor(std::log2(limit))));
                        }
                        ringZoom = std::max(0, ringZoom);
                    }
                    // The cover's footprint at the ring's zoom, widened by the margin.
                    std::vector<vt::TileId> candidates;
                    {
                        int minX = 0, minY = 0, maxX = 0, maxY = 0;
                        bool first = true;
                        for (const vt::TileId& tileId : coverTileIds) {
                            int shift = tileId.zoom - ringZoom;
                            int x = (shift >= 0 ? tileId.x >> shift : tileId.x << -shift);
                            int y = (shift >= 0 ? tileId.y >> shift : tileId.y << -shift);
                            if (first) {
                                minX = maxX = x;
                                minY = maxY = y;
                                first = false;
                            } else {
                                minX = std::min(minX, x); maxX = std::max(maxX, x);
                                minY = std::min(minY, y); maxY = std::max(maxY, y);
                            }
                        }
                        if (!first) {
                            for (int y = minY - casterMargin; y <= maxY + casterMargin; y++) {
                                for (int x = minX - casterMargin; x <= maxX + casterMargin; x++) {
                                    candidates.emplace_back(ringZoom, x, y);
                                }
                            }
                        }
                    }
                    std::stable_sort(candidates.begin(), candidates.end(), [](const vt::TileId& a, const vt::TileId& b) { return a.zoom > b.zoom; });
                    std::vector<vt::TileId> pending;
                    for (const vt::TileId& candidate : candidates) {
                        pending.assign(1, candidate);
                        while (!pending.empty()) {
                            vt::TileId tileId = pending.back();
                            pending.pop_back();
                            if (taken.count(keyOf(tileId)) > 0) {
                                continue;
                            }
                            bool insideTaken = false;
                            for (int zoom = tileId.zoom - 1; zoom >= 0 && !insideTaken; zoom--) {
                                int shift = tileId.zoom - zoom;
                                insideTaken = taken.count(TileKey(zoom, { tileId.x >> shift, tileId.y >> shift })) > 0;
                            }
                            if (insideTaken) {
                                continue; // something finer or equal already casts over this ground
                            }
                            if (takenAncestors.count(keyOf(tileId)) > 0 && tileId.zoom < maxCoverZoom) {
                                // BOUNDED. Subdividing a ring tile down to maxCoverZoom is 4^(levels)
                                // tiles - from a ring generated at zoom 10 against a cover at 18 that
                                // is 65536 per candidate, which is an out-of-memory kill rather than
                                // a slow frame. The ring exists so off-screen relief still casts;
                                // dropping a candidate that would blow the budget loses one distant
                                // shadow, which is strictly better than losing the process.
                                if (casterTileIds.size() + pending.size() < MAX_SHADOW_CASTER_TILES) {
                                    for (int corner = 0; corner < 4; corner++) {
                                        pending.emplace_back(tileId.zoom + 1, tileId.x * 2 + (corner & 1), tileId.y * 2 + (corner >> 1));
                                    }
                                }
                                continue;
                            }
                            take(tileId);
                            casterTileIds.push_back(tileId);
                        }
                    }
                }
                // The slab has to hold the CASTERS, and vt has no per-tile heights for the RING -
                // it measures every ring tile at this range, so a ridge taller than it is clipped
                // out of the caster pass by the light box's near plane and its shadow arrives
                // truncated, along an edge that moves with the camera because the range follows the
                // cover. Measured at Grenoble z16.53 tilt 90: the cover's range was 5.75..17.43
                // while the ring tiles reached 145.13. Taken from the caster tiles themselves, so
                // it is exact rather than a guess from an ancestor. The per-tile ranges above still
                // narrow each cascade's RECEIVER slab, so the texel size does not pay for this.
                if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                    for (const vt::TileId& tileId : casterTileIds) {
                        double casterMin = 0, casterMax = 0;
                        if (elevationManager->getMinMaxDisplayHeightCached(MapTile(tileId.x, tileId.y, tileId.zoom, 0), casterMin, casterMax) && casterMax > casterMin) {
                            minHeight = std::min(minHeight, casterMin);
                            maxHeight = std::max(maxHeight, casterMax);
                        }
                    }
                }
                // One light box per cascade, near slice first. A single box has to
                // span everything visible, so at a tilt its texels are metres of
                // ground and every shadow edge is a staircase; the near cascade
                // spends the same texels on a much smaller region.
                int cascades = _terrainShadowMap->getCascades();
                bool boxesValid = true;
                // The tiles that can cast into each cascade, which for a near cascade
                // is a fraction of the cover: drawing the rest into it is pure cost.
                std::array<std::vector<vt::TileId>, TerrainShadowMap::MAX_CASCADES> cascadeCasterTiles;
                for (int cascade = 0; cascade < cascades; cascade++) {
                    double depthRangeMeters = 1.0, texelMeters = 0;
                    if (tileLayers.front()->calculateShadowViewProj(coverTileIds, casterTileIds, shadowSunDir, tileHeights, minHeight, maxHeight, lighting.shadowDistance, cglib::length(viewState.getCameraPos() - viewState.getFocusPos()), _terrainShadowMap->getSize(), cascade, cascades, cascadeCasterTiles[cascade], depthRangeMeters, texelMeters, lightViewProjs[cascade])) {
                        // The bias is metric; the shader wants a fraction of the
                        // normalised light depth, and each cascade's box spans its
                        // own depth. Dividing per cascade is what keeps the shadow
                        // attached to its caster at every zoom and margin.
                        shadowTexelMeters[cascade] = texelMeters;
                        shadowDepthScales[cascade] = static_cast<float>(1.0 / std::max(1.0, depthRangeMeters));
                    } else if (cascade > 0) {
                        // No ground in this cascade's distance slice - looking down,
                        // everything visible can be nearer than the first split.
                        // Repeating the near box keeps the atlas layout intact and
                        // costs one redundant page; leaving it stale would shadow
                        // with a box from another frame.
                        lightViewProjs[cascade] = lightViewProjs[cascade - 1];
                        cascadeCasterTiles[cascade] = cascadeCasterTiles[cascade - 1];
                    } else {
                        boxesValid = false;
                        static int lastFitFailure = 0;
                        if (static_cast<int>(texelMeters) != lastFitFailure) {
                            lastFitFailure = static_cast<int>(texelMeters);
                            Log::Infof("MapRenderer: shadow light box could not be fitted, reason %d (1 no tiles, 2 tile bbox empty, 3 no elevation texture, 4 empty cascade slice, 5 slice misses the tiles, 6 sun below horizon)", lastFitFailure);
                        }
                        break;
                    }
                }
                if (boxesValid) {
                    // The caster pass costs as much as the on-screen draw, and the snapped light
                    // matrix repeats while the camera moves inside one texel step - so recompute
                    // only on a real change. A growing extrusion changes the geometry without
                    // changing the tile list, so track how far it has MOVED, not that it moves.
                    // PER CASCADE, over that cascade's own caster tiles: a building appearing in
                    // the near page has nothing to say about the outer ones, and refreshing all of
                    // them for it was most of the cost. Measured on the Crosscall (Adreno 610): a
                    // caster pass is ~8 ms settled and ~33 ms cold, so the number of pages redrawn
                    // per arriving tile is what this budget is made of.
                    std::array<float, TerrainShadowMap::MAX_CASCADES> fadeSignatures = { };
                    for (int cascade = 0; cascade < cascades; cascade++) {
                        for (const std::shared_ptr<TileLayer>& tileLayer : tileLayers) {
                            fadeSignatures[cascade] = std::max(fadeSignatures[cascade], tileLayer->shadowCasterFadeSignature(&cascadeCasterTiles[cascade]));
                        }
                    }
                    // The atlas layout itself changed: every page has to be redrawn.
                    bool refreshAll = !_shadowMapValid
                        || _shadowMapSize != _terrainShadowMap->getSize()
                        || _shadowMapCascades != cascades;
                    _shadowMapAge++;
                    // Content-driven refreshes are RATIONED, camera-driven ones are
                    // not. A shadow left behind by a moving camera is in the wrong
                    // place and unmissable; a building whose shadow is a step behind
                    // its own growth is not.
                    if (!refreshAll && contentChanged && _shadowMapAge >= SHADOW_MAP_CONTENT_INTERVAL) {
                        refreshAll = true;
                    }
                    if (!refreshAll && _shadowMapAge >= SHADOW_MAP_MAX_AGE) {
                        refreshAll = true;
                    }
                    // Otherwise it is per PAGE: each cascade's box is snapped to its own lattice,
                    // and the outer one - which holds most of the casters, since its box covers the
                    // whole view - keeps its matrix over far more camera movement than the near one.
                    // Redrawing all three because the near box stepped was most of the pass cost.
                    std::array<bool, TerrainShadowMap::MAX_CASCADES> refreshCascade = { };
                    bool refreshAny = false;
                    for (int cascade = 0; cascade < cascades; cascade++) {
                        refreshCascade[cascade] = refreshAll
                            || std::abs(fadeSignatures[cascade] - _shadowMapFadeSignatures[cascade]) > SHADOW_MAP_FADE_STEP
                            || !(_shadowMapViewProjs[cascade] == lightViewProjs[cascade])
                            || _shadowMapCasterTiles[cascade] != cascadeCasterTiles[cascade];
                        refreshAny = refreshAny || refreshCascade[cascade];
                    }
                    if (refreshAny) {
                        std::chrono::steady_clock::time_point shadowStart = std::chrono::steady_clock::now();
                        FRAME_PROF_GPU_BEGIN(SECTION_SHADOWCAST);
                        if (_terrainShadowMap->beginPass(refreshAll)) {
                            for (int cascade = 0; cascade < cascades; cascade++) {
                                if (!refreshCascade[cascade]) {
                                    continue;
                                }
                                // The cascades are pages of one texture, so each one draws into its
                                // own viewport, and a page redrawn on its own clears just itself.
                                _terrainShadowMap->setCascadeViewport(cascade);
                                if (!refreshAll) {
                                    _terrainShadowMap->clearCascade();
                                }
                                // EVERY drape layer casts, not just the first. The terrain
                                // surface is shared, but 3D extrusions belong to whichever
                                // layer holds them - in a composite that is a later style
                                // group, so casting from the front layer alone means
                                // buildings never cast a shadow at all.
                                for (const std::shared_ptr<TileLayer>& tileLayer : tileLayers) {
                                    bool castGround = (tileLayer == tileLayers.front());
                                    int draws = tileLayer->renderShadowCasters(cascadeCasterTiles[cascade], lightViewProjs[cascade], castGround);
                                    // Ground casters are one draw per tile; anything
                                    // beyond that is an extrusion. Counted separately
                                    // because "buildings cast no shadow" has two very
                                    // different causes - not drawn into the map at all,
                                    // or drawn and then clipped by the light box - and
                                    // only this tells them apart.
                                    shadowExtrusionDraws += draws - (castGround ? static_cast<int>(cascadeCasterTiles[cascade].size()) : 0);
                                    shadowCastersNoElevation += tileLayer->consumeShadowCastersMissingElevation();
                                }
                                _shadowMapViewProjs[cascade] = lightViewProjs[cascade];
                                _shadowMapCasterTiles[cascade] = cascadeCasterTiles[cascade];
                                _shadowMapFadeSignatures[cascade] = fadeSignatures[cascade];
                                shadowCasterDraws += static_cast<int>(cascadeCasterTiles[cascade].size());
                            }
                            _terrainShadowMap->endPass(prevFBO, viewState.getWidth(), viewState.getHeight());
                            FRAME_PROF_GPU_END();
                            shadowMsSum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shadowStart).count();
                            _shadowMapValid = true;
                            _shadowMapSize = _terrainShadowMap->getSize();
                            _shadowMapCascades = cascades;
                            _shadowMapAge = 0;
                            shadowPasses++;
                        } else {
                            FRAME_PROF_GPU_END();
                            _shadowMapValid = false;
                        }
                    }
                    if (_shadowMapValid) {
                        // The matrices the PAGES were drawn with, not this frame's fit: a page that
                        // did not need refreshing holds the box it was rendered with, and sampling
                        // it with a newer matrix would slide every shadow in it.
                        lightViewProjs = _shadowMapViewProjs;
                        shadowTexture = _terrainShadowMap->getTexture();
                        shadowMapSize = _terrainShadowMap->getSize();
                        shadowCascades = cascades;
                        shadowStrength = lighting.shadowStrength;
                        shadowSoftness = lighting.shadowSoftness;
                    }
                } else if (_shadowMapValid && _shadowMapAge < SHADOW_MAP_MAX_AGE) {
                    // A frame whose light box could not be fitted (a cascade with no
                    // ground in its slice, a cover with no decoded elevation yet) used
                    // to drop the shadows entirely for that frame - every shadow on
                    // screen blinking out and back. The last good map with the matrices
                    // it was rendered with is a far better answer than none.
                    //
                    // Only while it is still RECENT, though. Held without a bound it stops
                    // being one camera step stale and becomes a different scene: zooming out
                    // past the zoom a style stops extruding at, the fit has nothing to fit and
                    // the last map kept painting the shadows of buildings that were no longer
                    // drawn - black blocks over a flat map, for as long as you stayed there.
                    lightViewProjs = _shadowMapViewProjs;
                    shadowTexture = _terrainShadowMap->getTexture();
                    shadowMapSize = _shadowMapSize;
                    shadowCascades = _shadowMapCascades;
                    shadowStrength = lighting.shadowStrength;
                    shadowSoftness = lighting.shadowSoftness;
                    _shadowMapAge++;
                } else {
                    _shadowMapValid = false; // too old to stand in for a fit that keeps failing
                }
            }
        }
        if (!shadowsWanted) {
            _shadowMapValid = false; // shadows off: whatever the map holds is stale
        }
        // Shadows going away is otherwise indistinguishable from shadows being drawn
        // badly. Logged on CHANGE only, so it is one line per transition, not spam.
        {
            int shadowState = (!shadowsWanted ? 0 : (shadowTexture == 0 ? 1 : 2));
            static int lastShadowState = -1;
            if (shadowState != lastShadowState) {
                lastShadowState = shadowState;
                Log::Infof("MapRenderer: shadows %s (strength %.2f, requested map %d x %d cascades, terrain lighting %d, cover tiles %d)",
                    shadowState == 2 ? "ACTIVE" : shadowState == 1 ? "WANTED BUT UNAVAILABLE - no light box could be fitted, or the atlas failed to allocate" : "off",
                    lighting.shadowStrength, lighting.shadowMapSize, lighting.shadowCascades,
                    lighting.terrainLightingEnabled ? 1 : 0, static_cast<int>(coverTileIds.size()));
            }
        }
        // Where the outermost cascade fades out, as a view depth: mapbox's u_shadow_fade_range,
        // [far * 0.75, far] against the same cutout the boxes are cut at, so the fade always ends
        // exactly where the shadow map does. In internal units, which is what 1 / gl_FragCoord.w is.
        cglib::vec2<float> shadowFadeRange(0.0f, 0.0f);
        {
            double distanceFactor = lighting.shadowDistance > 0 ? lighting.shadowDistance : vt::GLTileRenderer::SHADOW_CUTOUT_DISTANCE_FACTOR;
            double cutout = distanceFactor * cglib::length(viewState.getCameraPos() - viewState.getFocusPos());
            if (cutout > 0) {
                shadowFadeRange = cglib::vec2<float>(static_cast<float>(cutout * vt::GLTileRenderer::SHADOW_FADE_START_FRACTION), static_cast<float>(cutout));
            }
        }
        for (const std::shared_ptr<TileLayer>& tileLayer : tileLayers) {
            // The SHADOW sun, not the lighting one: the normal offset is scaled by the angle
            // between the surface and the direction the map was actually rendered from.
            tileLayer->setTerrainShadowMap(shadowTexture, shadowMapSize, shadowCascades, shadowBias, shadowDepthScales, shadowStrength, shadowSoftness, _terrainShadowMap && _terrainShadowMap->isDepthTexture(), _terrainShadowMap && _terrainShadowMap->isHardwarePCF(), lighting.shadowNormalOffset, shadowFadeRange, shadowSunDir, lightViewProjs);
            // The sun goes with it, and for the same reason: the surface is drawn a few
            // lines below, while each layer's own onDrawFrame - which also sets this -
            // runs later in the frame. The surface would light itself with the previous
            // frame's sun, so toggling the light did nothing until something else
            // happened to force another frame.
            tileLayer->setTerrainSunLighting(lighting);
        }
        // Resolve the terrain's shadow ONCE per screen pixel, at a fraction of the screen
        // resolution, so the surface
        // draws - the drape, and the paint over it - each cost one texture fetch instead of a
        // cascade choice, a matrix, derivatives and four taps over the whole screen.
        unsigned int maskTexture = 0;
        float invWidth = 0.0f, invHeight = 0.0f;
        if (shadowTexture != 0 && viewState.getWidth() > 0 && viewState.getHeight() > 0) {
            if (!_terrainShadowMaskBuffer) {
                _terrainShadowMaskBuffer = std::make_unique<ScreenMaskBuffer>();
            }
            _terrainShadowMaskBuffer->setSize(viewState.getWidth(), viewState.getHeight(), SHADOW_MASK_DIVISOR);
            FRAME_PROF_GPU_BEGIN(SECTION_SHADOWMASK);
            if (_terrainShadowMaskBuffer->beginPass()) {
                // The mask is produced by the FIRST layer alone: the surface is shared, so every
                // layer would draw the same geometry into it.
                int maskDraws = tileLayers.front()->renderTerrainShadowMask(coverTileIds);
                _terrainShadowMaskBuffer->endPass(prevFBO, viewState.getWidth(), viewState.getHeight());
                maskTexture = _terrainShadowMaskBuffer->getTexture();
                {
                    static int probe = 0;
                    if ((probe++ % 121) == 120) {
                        Log::Infof("PROBE mask: texture %u, %d x %d, draws %d, cover %d", maskTexture, _terrainShadowMaskBuffer->getWidth(), _terrainShadowMaskBuffer->getHeight(), maskDraws, static_cast<int>(coverTileIds.size()));
                    }
                }
                // Screen pixels -> mask uv. The scale is the SCREEN size, not the mask's, because
                // it maps gl_FragCoord of the full-resolution draw that samples it.
                invWidth = 1.0f / viewState.getWidth();
                invHeight = 1.0f / viewState.getHeight();
            }
            FRAME_PROF_GPU_END();
        }
        for (const std::shared_ptr<TileLayer>& tileLayer : tileLayers) {
            tileLayer->setTerrainShadowMask(maskTexture, invWidth, invHeight);
        }
    }

    bool MapRenderer::coversTile(const vt::TileId& tileId, const vt::TileId& other) {
        if (tileId.zoom >= other.zoom) {
            return false; // strict ancestor only
        }
        int deltaZoom = other.zoom - tileId.zoom;
        return (other.x >> deltaZoom) == tileId.x && (other.y >> deltaZoom) == tileId.y;
    }

    std::vector<vt::TileId> MapRenderer::collectTerrainCoverTileIds(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions) const {
        std::vector<vt::TileId> tileIds;
        if (_terrainRenderer) {
            std::vector<MapTile> terrainTiles;
            _terrainRenderer->collectVisibleTiles(viewState, terrainOptions, terrainTiles);
            tileIds.reserve(terrainTiles.size());
            for (const MapTile& terrainTile : terrainTiles) {
                tileIds.emplace_back(terrainTile.getZoom(), terrainTile.getX(), terrainTile.getY());
            }
        }
        return tileIds;
    }

    void MapRenderer::collectTerrainCover(const std::vector<std::shared_ptr<TileLayer> >& tileLayers, const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::vector<vt::TileId>& seedTileIds, bool extendSeedsOnly, std::vector<std::map<vt::TileId, std::size_t> >& layerTiles, std::map<vt::TileId, std::size_t>& collectedTiles, std::vector<vt::TileId>& leaves, int& coverZoom, int& maxCollectedZoom) {
        // Collected PER LAYER, then merged. The union is what the cover is built from,
        // but which layers actually have something to bake for a tile is what tells a
        // texture baked from the full stack apart from one baked while only the
        // hillshade had arrived - and the second kind must not sit around looking
        // finished. A layer with nothing for a tile reports fingerprint 0.
        layerTiles.assign(tileLayers.size(), std::map<vt::TileId, std::size_t>());
        for (std::size_t i = 0; i < tileLayers.size(); i++) {
            tileLayers[i]->collectDrapeTiles(layerTiles[i]);
        }
        for (std::size_t i = 0; i < layerTiles.size(); i++) {
            for (auto it = layerTiles[i].begin(); it != layerTiles[i].end(); it++) {
                std::size_t& fingerprint = collectedTiles[it->first];
                fingerprint ^= it->second + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2);
            }
        }
        // Ground the cover has to cover, whatever the layers happen to hold. The layers' tiles
        // follow their own fetching, so right after a zoom OUT they still describe the small area
        // the previous zoom showed - and a cover built from them alone leaves most of the screen
        // with no terrain at all, falling through to the flat background plane. That is the
        // "tiles blink white while zooming" report. The terrain's own cover is camera-driven and
        // always covers the view, which is where tangram takes its ground tiles from.
        //
        // `extendSeedsOnly` is the drape's version of the same seed. Every leaf there costs a cache
        // texture and a bake, so it takes only the seeds that reach DEEPER than any tile the layers
        // gave: where the data already follows the camera the cover is exactly what it was, and the
        // seed only adds levels past a source's maxzoom - which is where the drape used to freeze
        // while the live geometry stayed sharp. With no data at all it seeds nothing, rather than
        // baking a screenful of blank textures.
        int dataMaxZoom = -1;
        for (auto it = collectedTiles.begin(); it != collectedTiles.end(); it++) {
            dataMaxZoom = std::max(dataMaxZoom, it->first.zoom);
        }
        if (!(extendSeedsOnly && dataMaxZoom < 0)) {
            for (const vt::TileId& tileId : seedTileIds) {
                if (extendSeedsOnly && tileId.zoom <= dataMaxZoom) {
                    continue;
                }
                collectedTiles.emplace(tileId, static_cast<std::size_t>(0));
            }
        }
        // A layer that bakes something not made of tiles - a terrain paint - cannot
        // contribute a cover, so a stack of nothing but such layers (a hillshade-only
        // map) would have no ground to paint on at all. The terrain's own tile cover
        // is the right one there: it is what the surface would be drawn from anyway.
        if (collectedTiles.empty()) {
            bool wantsCover = false;
            for (const std::shared_ptr<TileLayer>& tileLayer : tileLayers) {
                wantsCover = wantsCover || tileLayer->paintsEveryDrapeTile();
            }
            if (wantsCover && _terrainRenderer) {
                std::vector<MapTile> terrainTiles;
                _terrainRenderer->collectVisibleTiles(viewState, terrainOptions, terrainTiles);
                std::shared_ptr<ElevationManager> coverElevationManager = terrainOptions->getElevationManager();
                for (const MapTile& terrainTile : terrainTiles) {
                    collectedTiles[vt::TileId(terrainTile.getZoom(), terrainTile.getX(), terrainTile.getY())] = 0;
                    // Nothing else asks for elevation in this stack: the layers that
                    // normally drive it are the ones with tiles, and a paint has none.
                    // Without this the terrain stays flat and the paint has nothing to
                    // shade - the map is an empty grid.
                    if (coverElevationManager) {
                        MapTile dataTile = coverElevationManager->getDataTile(terrainTile);
                        coverElevationManager->prefetchTileGrid(dataTile, 2);
                        // And keep the frames coming until it arrives: in a stack with
                        // no tile layer nothing else asks for a redraw, so the map goes
                        // idle on a flat, unpainted terrain and never comes back.
                        if (!coverElevationManager->getDataTileGrid(dataTile, ElevationManager::LoadMode::CACHED_ONLY)) {
                            requestRedraw();
                        }
                    }
                }
            }
        }

        // Normalize the per-layer union to a non-overlapping quadtree partition, keeping the
        // finest tile for any ground - overlapping surfaces of different tesselations fight.
        // See docs/internals/rendering/04-terrain.md, "Normalizing the cover to a quadtree partition".
        std::vector<vt::TileId> pending;
        for (auto it = collectedTiles.begin(); it != collectedTiles.end(); it++) {
            bool hasCoarserTile = false;
            for (auto it2 = collectedTiles.begin(); it2 != collectedTiles.end() && !hasCoarserTile; it2++) {
                hasCoarserTile = coversTile(it2->first, it->first);
            }
            if (!hasCoarserTile) {
                pending.push_back(it->first); // top of a subtree; its descendants follow from the split
            }
        }
        static const std::size_t MAX_DRAPE_TILES = 256; // splitting is bounded; a runaway cover is not worth drawing
        int minTopZoom = 99;
        maxCollectedZoom = 0;
        for (auto it = collectedTiles.begin(); it != collectedTiles.end(); it++) {
            maxCollectedZoom = std::max(maxCollectedZoom, it->first.zoom);
        }
        for (const vt::TileId& tileId : pending) {
            minTopZoom = std::min(minTopZoom, tileId.zoom);
        }
        // The split level. The plain maximum over collected tiles is wrong: zooming out, a render
        // tile from before the gesture is still 'visible' while it blends away and would drag the
        // whole cover several levels finer than the camera. Cap it at what the camera can show.
        // TRIED AND REVERTED for the shared ground, where there is no texture budget to respect:
        // following the finest collected tile instead makes a zoom OUT explode the split, hit the
        // leaf cap, and truncate the cover - most of the screen then falls through to the flat
        // background plane, which is the "tiles blink white" report. The cap is not about textures.
        int viewZoomCap = static_cast<int>(std::ceil(viewState.getZoom())) + 1;
        coverZoom = std::min(maxCollectedZoom, std::max(viewZoomCap, minTopZoom));
        // Split ONLY where a finer collected tile sits inside: splitting whole subtrees to one
        // level made 16 tiles into 127 leaves and blew the drape cache (04-terrain.md).
        std::vector<vt::TileId> tops = pending;
        auto buildLeaves = [&](int zoomLimit) {
            leaves.clear();
            std::vector<vt::TileId> stack = tops;
            while (!stack.empty() && leaves.size() + stack.size() <= MAX_DRAPE_TILES) {
                vt::TileId tileId = stack.back();
                stack.pop_back();
                bool finerInside = false;
                for (auto it = collectedTiles.begin(); it != collectedTiles.end() && !finerInside; it++) {
                    finerInside = coversTile(tileId, it->first);
                }
                if (!finerInside || tileId.zoom >= zoomLimit) {
                    leaves.push_back(tileId);
                    continue;
                }
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        stack.push_back(tileId.getChild(dx, dy));
                    }
                }
            }
            // Cap hit: keep what is left coarse rather than lose the ground.
            leaves.insert(leaves.end(), stack.begin(), stack.end());
            return leaves.size();
        };
        while (buildLeaves(coverZoom) > MAX_DRAPE_TILES && coverZoom > minTopZoom) {
            coverZoom--; // one level coarser everywhere beats a half-split cover
        }
    }

    StyleEnvironment MapRenderer::collectStyleEnvironment(const ViewState& viewState) const {
        StyleEnvironment styleEnvironment;
        for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
            if (auto tileLayer = std::dynamic_pointer_cast<TileLayer>(layer)) {
                StyleEnvironment layerEnvironment;
                if (tileLayer->getStyleEnvironment(viewState, layerEnvironment)) {
                    styleEnvironment.mergeMissing(layerEnvironment);
                }
            }
        }
        return styleEnvironment;
    }

    void MapRenderer::drawLayers(float deltaSeconds, const ViewState& viewState, bool postProcessing) {
        FRAME_PROF_NOW(profDrawStart);
        FRAME_PROF_GPU_BEGIN(SECTION_PRELUDE);
        std::vector<std::shared_ptr<Layer> > layers = _layers->getAll();

        // Layers that opted out of post-processing are held back and drawn by drawOverlayLayers
        // once the effect has resolved. They are out of the whole terrain arrangement below
        // (depth write assignment, draping, the shared ground) on purpose - they are overlays.
        _overlayLayers.clear();
        if (postProcessing) {
            auto overlay = std::stable_partition(layers.begin(), layers.end(), [](const std::shared_ptr<Layer>& layer) {
                return layer->isPostProcessed();
            });
            _overlayLayers.assign(overlay, layers.end());
            layers.erase(overlay, layers.end());
        }

        // Terrain depth source: the FIRST suitable tile layer writes the depth of its
        // draped background/raster surfaces - the depth source is then bit-exact with the
        // rendered terrain, so draped geometry, other layers and vector elements can
        // depth-test against it without mesh-mismatch artifacts (sinking/see-through).
        // Only when no tile layer is available, a separate approximate terrain depth
        // pre-pass is rendered instead.
        bool terrainMode = false;
        if (_options->getRenderProjectionMode() == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
            if (auto terrainOptions = _options->getTerrainOptions()) {
                if (terrainOptions->isActive()) {
                    terrainMode = true;
                    // Elevation arrives on a loading thread and every consumer reads it from
                    // inside a frame, so the tiles that land after the last one are never
                    // applied: the map sits on a half-displaced mesh until the next gesture.
                    if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                        if (_redrawElevationManager.lock() != elevationManager) {
                            std::weak_ptr<MapRenderer> mapRendererWeak = shared_from_this();
                            elevationManager->setDataChangedListener([mapRendererWeak]() {
                                if (auto mapRenderer = mapRendererWeak.lock()) {
                                    mapRenderer->requestRedraw();
                                }
                            });
                            _redrawElevationManager = elevationManager;
                        }
                    }
                    bool depthWriteAssigned = false;
                    int terrainRenderOrder = 0;
                    for (const std::shared_ptr<Layer>& layer : layers) {
                        if (auto tileLayer = std::dynamic_pointer_cast<TileLayer>(layer)) {
                            bool depthWrite = !depthWriteAssigned && tileLayer->isVisible() && tileLayer->getOpacity() >= 1.0f;
                            tileLayer->setTerrainDepthWriteMode(depthWrite);
                            // stacking order for the fixed per-layer depth separation in GPU draping mode
                            tileLayer->setTerrainRenderOrder(terrainRenderOrder++);
                            depthWriteAssigned = depthWriteAssigned || depthWrite;
                        }
                    }
                    // Terrain base fill, before all tile layers so it shows through translucent
                    // content whatever the stacking order. COLOR-ONLY under a depth-writing tile
                    // layer - kept fill depth clips the differently-tesselated content in patches;
                    // without one it is the depth source for element/billboard occlusion.
                    bool depthSourceRendered = false;
                    {
                        if (!_terrainRenderer) {
                            _terrainRenderer = std::make_unique<TerrainRenderer>();
                        }
                        bool keepDepth = !depthWriteAssigned;
                        bool backgroundRendered = false;
                        // A surface shader paints the terrain itself and takes precedence over
                        // the bitmap/color fill. The sun and the fog it gets are the resolved
                        // ones (style Map block over LightOptions/TerrainOptions), so a shaded
                        // surface, the vt content and the sky agree on the light.
                        if (!terrainOptions->getSurfaceShaderSource().empty()) {
                            // The shaded surface is the case where there may be no tile layer at
                            // all - and the layers are what normally drive the elevation loads, so
                            // without this the surface has a flat height field to shade and the map
                            // goes idle on it (same reason as the terrain paint cover below).
                            if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                                std::vector<MapTile> terrainTiles;
                                _terrainRenderer->collectVisibleTiles(viewState, terrainOptions, terrainTiles);
                                for (const MapTile& terrainTile : terrainTiles) {
                                    MapTile dataTile = elevationManager->getDataTile(terrainTile);
                                    elevationManager->prefetchTileGrid(dataTile, 2);
                                    if (!elevationManager->getDataTileGrid(dataTile, ElevationManager::LoadMode::CACHED_ONLY)) {
                                        requestRedraw();
                                    }
                                }
                            }
                            ResolvedLighting surfaceLighting = resolveLighting(_options->getLightOptions(), _frameStyleEnvironment);
                            backgroundRendered = _terrainRenderer->renderSurface(viewState, terrainOptions, _glResourceManager, surfaceLighting, _frameFog, keepDepth);
                        }
                        if (!backgroundRendered && terrainOptions->isBackgroundBitmapEnabled()) {
                            if (std::shared_ptr<Bitmap> backgroundBitmap = _options->getBackgroundBitmap()) {
                                backgroundRendered = _terrainRenderer->renderBackground(viewState, terrainOptions, _glResourceManager, backgroundBitmap, keepDepth);
                            }
                        }
                        if (!backgroundRendered) {
                            Color terrainBackgroundColor = terrainOptions->getBackgroundColor();
                            if (terrainBackgroundColor.getA() > 0) {
                                backgroundRendered = _terrainRenderer->renderBackground(viewState, terrainOptions, _glResourceManager, terrainBackgroundColor, keepDepth);
                            }
                        }
                        depthSourceRendered = backgroundRendered && keepDepth;
                        if (!depthSourceRendered && !depthWriteAssigned) {
                            _terrainRenderer->renderDepthPrepass(viewState, terrainOptions, _glResourceManager);
                        }
                    }
                    if (terrainOptions->isBillboardOcclusionEnabled()) {
                        // Pixel-exact terrain depth buffer for label/billboard occlusion tests
                        if (!_terrainRenderer) {
                            _terrainRenderer = std::make_unique<TerrainRenderer>();
                        }
                        _terrainRenderer->updateDepthBuffer(viewState, terrainOptions, _glResourceManager);
                        if (_terrainRenderer->isDepthBufferStale()) {
                            // The refresh was deferred to keep the read-back stall out of a
                            // moving frame; keep asking for frames so it happens once the
                            // camera settles rather than on the next unrelated redraw.
                            requestRedraw();
                        }
                    }

                    // The clearance is a BOUND on the zoom, not a corrective event - a correction
                    // fights whatever drives the camera down and oscillates. mapbox's model
                    // (transform._constrainCamera): the clearance is a fraction of the camera's
                    // distance to sea level (CameraClearance), a zoom in stops at it, and a camera
                    // that got under it another way - a pan into a hillside, a DEM tile arriving -
                    // is LIFTED at a constant distance to the focus, so the zoom is kept and the
                    // tilt gives. Only a pan or a camera under the ground lifts (mapbox
                    // adaptCameraAltitude = dragging): after a zoom the ground under the moved
                    // camera differs by a little, and lifting for that turns every pinch tick into
                    // a tilt. See docs/internals/rendering/04-terrain.md.
                    {
                        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
                        float clampDuration = terrainOptions->getCameraClampDuration();
                        cglib::vec3<double> cameraPos = viewState.getCameraPos();
                        double displayScale = elevationManager->getDisplayScale(cameraPos(1));
                        double terrainZ = elevationManager->getDisplayHeight(cameraPos(0), cameraPos(1), ElevationManager::LoadMode::CACHED_ONLY);
                        double clearanceFloor = terrainOptions->getCameraClearance() * displayScale;
                        {
                            std::lock_guard<std::recursive_mutex> lock(_mutex);
                            _viewState.setTerrainCameraReference(terrainZ, clearanceFloor);
                        }
                        bool panned = _pannedSinceClearance.exchange(false);
                        double focusZ = viewState.getFocusPos()(2);
                        double orbit = viewState.getOrbitDistance(viewState.getZoom());
                        double minHeight = CameraClearance::minHeight(focusZ, orbit, viewState.getOrbitDistance(_options->getZoomRange().getMax()), clearanceFloor);
                        double cameraHeight = cameraPos(2) - terrainZ;
                        double deadBand = 0.005 * minHeight;
                        if (orbit > 0 && cameraHeight < minHeight - deadBand && (panned || cameraHeight < 0)) {
                            // The height above the focus that puts the camera on the shell, and the
                            // tilt that gives it (tilt 90 is straight down); past the tilt range's
                            // top the rest comes from zooming out, about the focus.
                            MapRange tiltRange = _options->getTiltRange();
                            double maxTiltSin = std::sin(tiltRange.getMax() * Const::DEG_TO_RAD);
                            double targetHeight = terrainZ + minHeight - focusZ;
                            float tilt = viewState.getTilt();
                            if (targetHeight <= orbit * maxTiltSin) {
                                tilt = static_cast<float>(std::asin(std::max(0.0, targetHeight / orbit)) * Const::RAD_TO_DEG);
                            } else {
                                tilt = tiltRange.getMax();
                                float maxZoom = CameraClearance::maxZoom(viewState.getZoom(), focusZ, focusZ + orbit * maxTiltSin, orbit, terrainZ,
                                                                         viewState.getOrbitDistance(_options->getZoomRange().getMax()), clearanceFloor);
                                float zoom = std::max(maxZoom, _options->getZoomRange().getMin());
                                if (zoom < viewState.getZoom() - 1.0e-4f) {
                                    CameraZoomEvent zoomEvent;
                                    zoomEvent.setZoomDelta(zoom - viewState.getZoom());
                                    // API, not GESTURE: the SDK corrects the camera here, and it does so
                                    // after a programmatic move just as much as after a gesture.
                                    calculateCameraEvent(zoomEvent, clampDuration, false, MapMoveReason::MAP_MOVE_REASON_API);
                                }
                            }
                            if (tilt > viewState.getTilt() + 1.0e-3f) {
                                CameraTiltEvent tiltEvent;
                                tiltEvent.setKeepRotation(true);
                                tiltEvent.setTilt(tilt);
                                calculateCameraEvent(tiltEvent, clampDuration, false, MapMoveReason::MAP_MOVE_REASON_API);
                            }
                        }
                    }
                }
            }
        }
        if (!terrainMode) {
            for (const std::shared_ptr<Layer>& layer : layers) {
                if (auto tileLayer = std::dynamic_pointer_cast<TileLayer>(layer)) {
                    tileLayer->setTerrainDepthWriteMode(false);
                }
            }
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _viewState.clearTerrainCameraReference(); // release the terrain zoom bound
        }

        // Cross-layer terrain draping. Every drapeable tile layer bakes into ONE texture per
        // terrain tile, in layer order, and a single surface draw puts that texture on the
        // terrain - so a hillshade layer and a vector tile layer share one drape, one surface and
        // one depth domain instead of each keeping its own. Content that is draped never enters
        // the 3D scene at all, which is what removes the whole content-vs-surface depth problem.
        // Whether the RTT drape actually carried the ground this frame, so the screen-space contact
        // shadow knows to stand down. collectDrapeLayers below returns every visible tile layer,
        // draped or not, and cannot answer this.
        bool groundAODraped = false;
        std::vector<std::shared_ptr<TileLayer> > drapeLayers;
        bool sharedGroundActive = false;
        if (terrainMode) {
            // A terrain paint has no tile set: without a drape to bake into it draws itself, on
            // the terrain's own cover. Pushed every frame, before any layer draws, and harmless
            // for a paint that does bake (it ignores the list).
            if (auto paintTerrainOptions = _options->getTerrainOptions()) {
                std::vector<std::shared_ptr<TileLayer> > paintLayers;
                for (const std::shared_ptr<Layer>& layer : layers) {
                    layer->collectDrapeLayers(paintLayers, viewState);
                }
                bool anyPaint = false;
                for (const std::shared_ptr<TileLayer>& tileLayer : paintLayers) {
                    anyPaint = anyPaint || tileLayer->paintsEveryDrapeTile();
                }
                if (anyPaint && _terrainRenderer) {
                    std::vector<MapTile> terrainTiles;
                    _terrainRenderer->collectVisibleTiles(viewState, paintTerrainOptions, terrainTiles);
                    std::vector<vt::TileId> paintTileIds;
                    paintTileIds.reserve(terrainTiles.size());
                    for (const MapTile& terrainTile : terrainTiles) {
                        paintTileIds.emplace_back(terrainTile.getZoom(), terrainTile.getX(), terrainTile.getY());
                    }
                    for (const std::shared_ptr<TileLayer>& tileLayer : paintLayers) {
                        if (tileLayer->paintsEveryDrapeTile()) {
                            tileLayer->setTerrainPaintTiles(paintTileIds);
                        }
                    }
                }
            }
            if (auto terrainOptions = _options->getTerrainOptions()) {
                if (terrainOptions->isDrapeFillsEnabled()) {
                    // Layers report their own drapeable tile layers, so a composite layer can
                    // contribute its children (hillshade/raster slots, style-layer groups) in
                    // draw order rather than only its own group-0 renderer.
                    for (const std::shared_ptr<Layer>& layer : layers) {
                        layer->collectDrapeLayers(drapeLayers, viewState);
                    }
                } else {
                    // NO DRAPE: the tangram arrangement. The stack still shares ONE cover, but
                    // nothing is baked - the ground is drawn once for that cover, here, before any
                    // layer, and every layer then composites straight onto it in layer order. What
                    // that removes is the whole bake (textures, budgets, stand-ins) AND, in every
                    // layer, its private depth domain and its stencil tile masks: one ground draw
                    // per tile instead of a pre-pass plus a mask per tile PER LAYER.
                    std::vector<std::shared_ptr<TileLayer> > groundLayers;
                    for (const std::shared_ptr<Layer>& layer : layers) {
                        layer->collectDrapeLayers(groundLayers, viewState);
                    }
                    if (!groundLayers.empty()) {
                        // Every layer's render tiles must exist before the cover is read from them.
                        FRAME_PROF_ADD(preludeMs, profDrawStart);
                        FRAME_PROF_NOW(profPrepareStart);
                        FRAME_PROF_GPU_BEGIN(SECTION_PREPARE);
                        for (const std::shared_ptr<TileLayer>& tileLayer : groundLayers) {
                            tileLayer->prepareTerrainDrapeFrame(deltaSeconds, viewState);
                        }
                        FRAME_PROF_ADD(prepareMs, profPrepareStart);
                        FRAME_PROF_NOW(profCoverStart);
                        FRAME_PROF_GPU_BEGIN(SECTION_COVER);

                        // The terrain's own visible cover seeds the ground: it is what the camera
                        // can see, not what the layers happen to have fetched.
                        std::vector<vt::TileId> terrainCoverTileIds = collectTerrainCoverTileIds(viewState, terrainOptions);
                        std::vector<std::map<vt::TileId, std::size_t> > groundLayerTiles;
                        std::map<vt::TileId, std::size_t> groundCollectedTiles;
                        std::vector<vt::TileId> groundTileIds;
                        std::vector<int> groundProxyDepths;
                        std::vector<bool> groundStandingIn; // parallel: this tile is drawn in place of a finer one
                        int groundZoom = 0, groundMaxCollectedZoom = 0;
                        collectTerrainCover(groundLayers, viewState, terrainOptions, terrainCoverTileIds, false, groundLayerTiles, groundCollectedTiles, groundTileIds, groundZoom, groundMaxCollectedZoom);

                        // A leaf whose DEM has not arrived is drawn FLAT, and the paint skips it
                        // (it has nothing to shade), so it flashes in the bare ground colour until
                        // the elevation lands - which during a zoom is every tile on screen, and
                        // reads as tiles blinking white then filling in. The drape hid this behind
                        // a stand-in texture from an ancestor; without textures the equivalent is
                        // to STAND ON the ancestor: keep the coarsest displaced ground that is
                        // actually loaded rather than introduce a flat one.
                        if (std::shared_ptr<ElevationManager> groundElevationManager = terrainOptions->getElevationManager()) {
                            auto hasElevation = [&groundElevationManager](const vt::TileId& tileId) {
                                int tileMask = (1 << tileId.zoom) - 1;
                                MapTile mapTile(tileId.x & tileMask, std::min(std::max(tileId.y, 0), tileMask), tileId.zoom, 0);
                                return static_cast<bool>(groundElevationManager->getTileGrid(mapTile, ElevationManager::LoadMode::CACHED_ONLY));
                            };
                            std::vector<vt::TileId> loadedTileIds;
                            std::vector<bool> standingIn;
                            loadedTileIds.reserve(groundTileIds.size());
                            standingIn.reserve(groundTileIds.size());
                            for (const vt::TileId& tileId : groundTileIds) {
                                vt::TileId standIn = tileId;
                                while (standIn.zoom > 0 && !hasElevation(standIn)) {
                                    standIn = standIn.getParent();
                                }
                                // The walk can bring several leaves onto one ancestor; drawing it
                                // once is both correct and cheaper.
                                auto it = std::find(loadedTileIds.begin(), loadedTileIds.end(), standIn);
                                if (it == loadedTileIds.end()) {
                                    loadedTileIds.push_back(standIn);
                                    standingIn.push_back(standIn != tileId);
                                } else if (standIn != tileId) {
                                    standingIn[it - loadedTileIds.begin()] = true;
                                }
                            }
                            groundTileIds = std::move(loadedTileIds);
                            groundStandingIn = std::move(standingIn);
                        }

                        // Tangram's proxy depth (tileManager.cpp). The `m_proxyCounter > 0` guard is
                        // the point: only a stand-in gets a depth, a live coarse tile takes zero.
                        // See docs/internals/rendering/05-depth-model.md, "Proxy depth".
                        int groundCoverZoom = 0;
                        for (const vt::TileId& tileId : groundTileIds) {
                            groundCoverZoom = std::max(groundCoverZoom, tileId.zoom);
                        }
                        groundStandingIn.resize(groundTileIds.size(), false);
                        groundProxyDepths.clear();
                        groundProxyDepths.reserve(groundTileIds.size());
                        for (std::size_t i = 0; i < groundTileIds.size(); i++) {
                            groundProxyDepths.push_back(groundStandingIn[i] ? std::max(groundCoverZoom - groundTileIds[i].zoom, 1) : 0);
                        }

                        // What the ground is painted with where no layer paints anything. Ground
                        // with a hole in it shows the flat map background plane BEHIND the terrain
                        // - the "landcover holes" - so it follows the drape's clear colour rule:
                        // the terrain's own background, else the first layer that defines one.
                        Color groundColor = terrainOptions->getBackgroundColor();
                        if (groundColor.getA() == 0) {
                            for (const std::shared_ptr<TileLayer>& tileLayer : groundLayers) {
                                Color layerColor = tileLayer->getBackgroundColor(viewState);
                                if (layerColor.getA() != 0) {
                                    groundColor = layerColor;
                                    break;
                                }
                            }
                        }

                        // One dense ordinal range per layer, in draw order, starting at 1 (ordinal 0
                        // is the ground). Dense because the TOTAL span sets the leak threshold, so a
                        // fixed stride reaches it within a few layers. A frame of lag in the counts
                        // is harmless - they only have to be consistent. docs/internals/rendering/05-depth-model.md.
                        int ordinalBase = 1;
                        for (const std::shared_ptr<TileLayer>& tileLayer : groundLayers) {
                            tileLayer->setExternalDrapeTarget(false);
                            tileLayer->setExternalDrapeTiles(std::vector<vt::TileId>());
                            tileLayer->setTerrainGroundTiles(groundTileIds, groundProxyDepths);
                            tileLayer->setTerrainLayerOrdinalBase(ordinalBase);
                            ordinalBase += std::max(1, tileLayer->getStyleLayerCount());
                        }
                        // The caster pass and the sun, over the same cover. Both have to be set
                        // BEFORE the ground is drawn: each layer normally sets the sun from its own
                        // onDrawFrame, which runs later in the frame, so the ground would light
                        // itself with the previous frame's sun. There is no bake here, so the
                        // content-driven refresh rides on the cover changing instead.
                        GLint groundPrevFBO = 0;
                        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &groundPrevFBO);
                        ResolvedLighting lighting;
                        std::array<double, TerrainShadowMap::MAX_CASCADES> shadowTexelMeters = { };
                        bool coverChanged = (_groundCoverTileIds != groundTileIds);
                        _groundCoverTileIds = groundTileIds;
                        // Shadows OFF for now, deliberately. Turning them on works - the Opera casts
                        // on its street and the buildings shadow each other - but the road overlay
                        // wears a fine speckle of its own acne over the whole map, which the drape
                        // path does not. Half-working shadows are worse than none; flip to true to
                        // work on it, with the drape path as the reference to diff against.
                        applyTerrainShadows(groundLayers, groundTileIds, terrainOptions, viewState, groundPrevFBO, coverChanged, false, lighting, shadowTexelMeters);

                        FRAME_PROF_ADD(coverMs, profCoverStart);
                        FRAME_PROF_NOW(profGroundStart);
                        FRAME_PROF_GPU_BEGIN(SECTION_DRAPE);
                        // The ground is drawn by the layer that PAINTS it when there is one: the
                        // paint and its lighting shader live on that layer's renderer, and in
                        // tangram's arrangement the shading is part of the ground draw rather than
                        // a surface over it. Any layer can draw a plain ground, so the first one
                        // does when nothing paints.
                        std::shared_ptr<TileLayer> groundDrawer = groundLayers.front();
                        for (const std::shared_ptr<TileLayer>& tileLayer : groundLayers) {
                            if (tileLayer->paintsEveryDrapeTile()) {
                                groundDrawer = tileLayer;
                                break;
                            }
                        }
                        int groundDraws = groundDrawer->renderTerrainGround(groundColor);
                        FRAME_PROF_ADD(drapeMs, profGroundStart);
                        FRAME_PROF_GPU_END();

                        sharedGroundActive = true;
                        // Periodically, and once for the first frame that actually has a cover: a
                        // map settles and stops drawing frames, so a plain frame counter can leave
                        // the only line in the log being the empty startup one.
                        static int groundStateFrame = 0;
                        static bool groundCoverLogged = false;
                        bool firstCover = !groundCoverLogged && !groundTileIds.empty();
                        groundCoverLogged = groundCoverLogged || firstCover;
                        if ((groundStateFrame++ % 600) == 1 || firstCover) {
                            Log::Infof("MapRenderer: shared terrain ground - %d layers, %d cover tiles (split level %d, collected up to %d, camera zoom %.2f), %d ground draws",
                                static_cast<int>(groundLayers.size()), static_cast<int>(groundTileIds.size()),
                                groundZoom, groundMaxCollectedZoom, viewState.getZoom(), groundDraws);
                        }
                    }
                }
                // A single stack for now: the usual configuration (hillshade under vector tiles)
                // is contiguous and entirely drapeable. Splitting into several stacks only
                // matters once a non-drapeable layer sits between drapeable ones.
                if (!drapeLayers.empty()) {
                    if (!_terrainDrapeCache) {
                        _terrainDrapeCache = std::make_unique<TerrainDrapeCache>();
                    }
                    _terrainDrapeCache->setResolution(TileRenderer::resolveDrapeResolution(terrainOptions->getDrapeResolution(), viewState, _options));
                    // WHICH layers bake, not what is in them. Switching the base map's style
                    // builds a new layer object, so the cached textures - including the ones held
                    // off screen for panning - are pictures of the previous style. They are only
                    // ever noticed through the per-tile fingerprint, which is re-baked at one tile
                    // per frame, so panning kept bringing the old style back a tile at a time.
                    // A layer that bakes something which is NOT made of its tiles - a terrain paint
                    // shading the elevation texture - has no per-tile fingerprint to be noticed
                    // through, so it folds its own appearance into this signature instead.
                    std::size_t stackSignature = 0;
                    for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                        std::size_t layerHash = tileLayer->drapeStackSignature();
                        stackSignature ^= layerHash + 0x9e3779b9 + (stackSignature << 6) + (stackSignature >> 2);
                    }
                    _terrainDrapeCache->setStackSignature(stackSignature);

                    // Every participating layer's render tiles must exist before any of them
                    // bakes, so start their frames first.
                    FRAME_PROF_ADD(preludeMs, profDrawStart);
                    FRAME_PROF_NOW(profPrepareStart);
                    FRAME_PROF_GPU_BEGIN(SECTION_PREPARE);
                    for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                        tileLayer->prepareTerrainDrapeFrame(deltaSeconds, viewState);
                    }
                    FRAME_PROF_ADD(prepareMs, profPrepareStart);
                    FRAME_PROF_NOW(profCoverStart);
                    FRAME_PROF_GPU_BEGIN(SECTION_COVER);

                    std::vector<std::map<vt::TileId, std::size_t> > layerTiles;
                    std::map<vt::TileId, std::size_t> collectedTiles;
                    std::vector<vt::TileId> leaves;
                    int drapeZoom = 0, maxCollectedZoom = 0;
                    // Seeded from the CAMERA where it reaches deeper than the layers do. Built from
                    // the collected tiles alone the cover cannot split past the deepest tile a
                    // source gave, so once the camera zooms past a source's maxzoom the drape's
                    // metres-per-texel freeze while the live geometry keeps its full precision -
                    // the ground goes soft and stays soft. mapbox-gl-js drapes onto a proxy source
                    // of its own (terrain.ts, maxzoom = the MAP's max zoom, reparseOverscaled);
                    // the terrain cover is ours, and it reaches floor(camera zoom) whatever the
                    // vector source stops at.
                    collectTerrainCover(drapeLayers, viewState, terrainOptions, collectTerrainCoverTileIds(viewState, terrainOptions), true, layerTiles, collectedTiles, leaves, drapeZoom, maxCollectedZoom);
                    std::map<vt::TileId, std::size_t> drapeTiles;
                    // Which drape layers have content to bake into each leaf right now. Compared
                    // against what the cached texture was actually baked from, this separates
                    // "the picture moved on a little" from "a whole layer is missing here".
                    std::map<vt::TileId, std::size_t> drapeTileLayerMasks;
                    // Whether the DEM for a tile is decoded YET. A terrain paint can only paint a
                    // tile that has elevation, so this decides both whether the tile is expected to
                    // carry the paint and - through the fingerprint below - that it is baked again
                    // once the elevation does arrive.
                    std::shared_ptr<ElevationManager> drapeElevationManager = terrainOptions->getElevationManager();
                    auto hasElevationData = [&drapeElevationManager](const vt::TileId& tileId) {
                        if (!drapeElevationManager) {
                            return true; // no elevation source at all: nothing is displaced
                        }
                        int tileMask = (1 << tileId.zoom) - 1;
                        MapTile mapTile(tileId.x & tileMask, std::min(std::max(tileId.y, 0), tileMask), tileId.zoom, 0);
                        return static_cast<bool>(drapeElevationManager->getTileGrid(mapTile, ElevationManager::LoadMode::CACHED_ONLY));
                    };
                    std::map<vt::TileId, bool> leafElevation;
                    bool anyPaintLayer = false;
                    for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                        anyPaintLayer = anyPaintLayer || tileLayer->paintsEveryDrapeTile();
                    }
                    for (const vt::TileId& tileId : leaves) {
                        bool paintable = hasElevationData(tileId);
                        leafElevation[tileId] = paintable;
                        std::size_t layerMask = 0;
                        for (std::size_t i = 0; i < layerTiles.size() && i < sizeof(std::size_t) * 8; i++) {
                            // A layer whose content is not made of tiles - a terrain paint - paints
                            // every tile it can and reports none of them, so it cannot be part of
                            // this mask: an incomplete tile is NOT DRAWN, and the paint is the
                            // layer most likely to be a frame late (its textures are prepared
                            // asynchronously). Gating on it blanked the top of the screen and the
                            // whole map during a zoom, where every tile is new. It gets a re-bake
                            // instead - through the fingerprint below when the elevation arrives,
                            // and through the bake itself when it could not paint (see bakeTile).
                            if (drapeLayers[i]->paintsEveryDrapeTile()) {
                                continue;
                            }
                            for (auto it = layerTiles[i].begin(); it != layerTiles[i].end(); it++) {
                                if (it->second == 0) {
                                    continue; // reported for the cover, but nothing drapeable in it
                                }
                                // Only what bakeDrapeTile will actually draw - its own tile or a
                                // COARSER one. Counting finer tiles (which it skips on purpose) made
                                // the leaf permanently incomplete, so stand-ins kept a patch of stale
                                // finer map alive across a zoom out.
                                if (it->first == tileId || coversTile(it->first, tileId)) {
                                    layerMask |= static_cast<std::size_t>(1) << i;
                                    break;
                                }
                            }
                        }
                        drapeTileLayerMasks[tileId] = layerMask;
                        // Fold in every collected tile that will bake here - the leaf itself, every
                        // coarser tile covering it, and (when the split hit the cap and left this
                        // leaf coarse) the finer tiles inside it, which bake into their sub-rect.
                        // A contributor left out here is content whose change never invalidates
                        // the texture, i.e. a tile that stays stale for as long as it is cached.
                        std::size_t fingerprint = 0;
                        auto exactIt = collectedTiles.find(tileId);
                        if (exactIt != collectedTiles.end()) {
                            fingerprint = exactIt->second;
                        }
                        for (auto it = collectedTiles.begin(); it != collectedTiles.end(); it++) {
                            if (coversTile(it->first, tileId) || coversTile(tileId, it->first)) {
                                fingerprint ^= it->second + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2);
                            }
                        }
                        if (anyPaintLayer) {
                            // The paint has no per-tile content to fingerprint, but whether it can
                            // paint this tile at all is per-tile: fold it in, so the tile is baked
                            // again the moment its elevation arrives.
                            std::size_t elevationTerm = (paintable ? 0x9e3779b9u : 0x85ebca6bu);
                            fingerprint ^= elevationTerm + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2);
                        }
                        // The style's own functions are evaluated at the VIEW zoom, so a texture
                        // baked at one zoom is wrong at another - a road's width, and anything else
                        // that interpolates. Fold the zoom in, quantised, so drifting past the
                        // threshold marks the tile stale and it is re-baked through the ordinary
                        // budget. _drapeBakeZoomTerm only follows the camera once it SETTLES, so a
                        // pinch does not re-bake every tile on every step.
                        fingerprint ^= _drapeBakeZoomTerm + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2);
                        drapeTiles[tileId] = fingerprint;
                    }

                    // Only take the surface away from the per-layer path once we know this frame
                    // actually has tiles to drape. Enabling external targets unconditionally
                    // suppresses each layer's pre-pass AND its own drape surface, so a frame that
                    // then draws no shared surface leaves the terrain with no surface at all -
                    // worse than not draping.
                    bool drapeActive = !drapeTiles.empty();
                    std::vector<vt::TileId> drapeTileIds;
                    drapeTileIds.reserve(drapeTiles.size());
                    for (auto it = drapeTiles.begin(); it != drapeTiles.end(); it++) {
                        drapeTileIds.push_back(it->first);
                    }
                    for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                        tileLayer->setExternalDrapeTarget(drapeActive);
                        // Tell every participating layer which ground is draped BEFORE it draws.
                        // This has to be an explicit per-frame hand-off: deriving it inside the
                        // renderer from the surface draw is fragile, because the layer's own
                        // startFrame runs between the two and resets frame state.
                        tileLayer->setExternalDrapeTiles(drapeActive ? drapeTileIds : std::vector<vt::TileId>());
                    }
                    // WHERE THE LIVE LAYERS SIT IN THE STACK (#175). The drape composite is drawn
                    // before any live geometry, so a layer kept out of the bake
                    // (TerrainOptions::NoDrapeLayerFilter) can only land on TOP of all of it -
                    // contours over roads in 3D, roads over contours in 2D. Flatten the whole stack
                    // into ordered units, one per style layer of each drape layer, and mark every
                    // LIVE unit that has a DRAPED unit after it: that unit is drawn through a mask
                    // holding the accumulated coverage of everything draped above it.
                    //
                    // Units sharing the same nearest draped unit share a mask, so the count is the
                    // number of live->draped transitions - 0 or 1 for every ordinary style.
                    //
                    // Capped: each mask is an R8 texture per drape tile and one more rasterisation
                    // of the units above it. A style needing more than this is pathological, and
                    // the cuts left out simply keep the pre-#175 behaviour. The rule itself is in
                    // terrain/DrapeStackCuts.h, where the host tests can reach it.
                    static const std::size_t MAX_DRAPE_COVERAGE_MASKS = 2;
                    std::vector<DrapeStackCuts::Cut> drapeCuts;
                    std::vector<std::map<int, int> > drapeLayerMasks(drapeLayers.size());
                    if (drapeActive && TerrainDrapeCache::isCoverageMaskEnabled()) {
                        std::vector<DrapeStackCuts::Unit> units;
                        for (std::size_t i = 0; i < drapeLayers.size(); i++) {
                            std::vector<std::pair<int, bool> > layerUnits;
                            drapeLayers[i]->collectDrapeStackOrder(layerUnits);
                            for (const std::pair<int, bool>& unit : layerUnits) {
                                units.push_back(DrapeStackCuts::Unit { i, unit.first, unit.second });
                            }
                        }
                        if (DrapeStackCuts::compute(units, MAX_DRAPE_COVERAGE_MASKS, drapeCuts, drapeLayerMasks)) {
                            static bool cutCapLogged = false;
                            if (!cutCapLogged) {
                                cutCapLogged = true;
                                Log::Warnf("MapRenderer: more than %d no-drape cuts in the style stack - the deepest are drawn on top, as before", static_cast<int>(MAX_DRAPE_COVERAGE_MASKS));
                            }
                        }
                    }
                    std::size_t drapeCutSignature = DrapeStackCuts::signature(drapeCuts);

                    if (!drapeActive) {
                        static bool emptyDrapeLogged = false;
                        if (!emptyDrapeLogged) {
                            emptyDrapeLogged = true;
                            Log::Info("MapRenderer: RTT drape has no tiles this frame - per-layer path retained");
                        }
                    }

                    // What the bake starts from. A texel no layer paints is a hole: the terrain
                    // surface is translucent there and the map background plane - which in terrain
                    // mode lies BEHIND the terrain - shows through, which is exactly what the
                    // "landcover holes" look like. Style layers only paint their own features, so
                    // the ground between them has to come from the background colour, baked in.
                    Color drapeClearColor = terrainOptions->getBackgroundColor();
                    if (drapeClearColor.getA() == 0) {
                        for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                            Color layerColor = tileLayer->getBackgroundColor(viewState);
                            if (layerColor.getA() != 0) {
                                drapeClearColor = layerColor;
                                break;
                            }
                        }
                    }

                    GLint prevFBO = 0;
                    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
                    std::chrono::steady_clock::time_point drapeStart = std::chrono::steady_clock::now();
                    FRAME_PROF_ADD(coverMs, profCoverStart);
                    FRAME_PROF_GPU_BEGIN(SECTION_DRAPE);
                    try {
                    _terrainDrapeCache->beginFrame();
                    struct DrapedTile { vt::TileId tileId; unsigned int texture; float uvOffsetX, uvOffsetY, uvScale; };
                    std::vector<DrapedTile> drapedTiles;
                    drapedTiles.reserve(drapeTiles.size());
                    int resolution = _terrainDrapeCache->getResolution();
                    bool bakeStarted = false;
                    // Offscreen state is entered once per frame and only when something actually
                    // has to be drawn into a drape texture.
                    auto beginOffscreen = [&]() {
                        if (bakeStarted) {
                            return;
                        }
                        glBindFramebuffer(GL_FRAMEBUFFER, _terrainDrapeCache->getFrameBuffer());
                        glViewport(0, 0, resolution, resolution);
                        glDisable(GL_DEPTH_TEST);
                        glDepthMask(GL_FALSE);
                        glDisable(GL_STENCIL_TEST);
                        bakeStarted = true;
                    };
                    // Cumulative since start: bakes are cached, so a per-frame count is 0 on most
                    // frames and says nothing about whether baking ever produced anything.
                    // Resolved once, not per tile: it locks two mutexes per layer.
                    bool groundAOWanted = false;
                    for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                        groundAOWanted = tileLayer->isGroundAOBakeable() || groundAOWanted;
                    }
                    static int bakedTiles = 0, bakedPrimitives = 0;
                    int surfaceDraws = 0, filledSurfaces = 0, skippedSurfaces = 0;
                    // Per-frame, unlike the cumulative counter above: the shadow cache below needs
                    // to know whether THIS frame produced new tile content, not whether any frame
                    // ever did.
                    int bakedThisFrame = 0;
                    // Per-frame bake budget, three urgency classes (hole / stand-in / merely
                    // stale). A bake is ~16 ms at 1024, so the budget IS the frame time - raising it
                    // to clear a renamed cover in one frame measured 128 ms -> 300 ms worst frame.
                    // docs/internals/rendering/04-terrain.md, "The drape cache".
                    static const int DRAPE_BAKE_BUDGET_BLANK = 8;
                    static const int DRAPE_BAKE_BUDGET_STANDIN = 3;
                    // A tile MISSING A WHOLE LAYER is a fourth case, and it is not the mild one the
                    // stale budget was sized for. Raster layers are ready as soon as their tile
                    // decodes while vector tiles take a style pass, so a tile baked mid-load holds
                    // the hillshade and nothing else - and at one re-bake per frame a zoom leaves
                    // dozens of them showing bare hillshade over the map for the best part of a
                    // second, which reads as the hillshade layer flashing on top of everything.
                    static const int DRAPE_BAKE_BUDGET_PARTIAL = 6;
                    static const int DRAPE_BAKE_BUDGET_STALE = 1;
                    // A tile baked from a layer stack that no longer exists (the base map's style
                    // was switched, a layer was turned off) shows the PREVIOUS MAP. One per frame
                    // is the budget for a tile that is merely out of date; here the picture is
                    // wrong, and since the cache keeps a generation of tiles alive off screen,
                    // panning kept walking back over them and flashing the old style tile by tile.
                    // Cleared at the blank-tile rate instead: a couple of frames, like a zoom.
                    static const int DRAPE_BAKE_BUDGET_RESTACK = 8;
                    // Wall-clock ceiling for all of the classes above together, per frame.
                    // Measured on an Adreno 610 from a cold start in 3D: 5-11 bakes a second get
                    // through against 10-51 queued, while the bakes themselves cost 12-31 ms a
                    // SECOND - one to three percent of the wall clock. The budget, not the work,
                    // is what makes roads and fills crawl into view in 3D when 2D shows them at
                    // once. Give a frame room for several bakes, and much more room when the
                    // camera is still: a longer frame is invisible on a map that is not moving,
                    // a tile that takes seconds to appear is not.
                    static const double DRAPE_BAKE_TIME_BUDGET = 16.0;       // ms, camera moving
                    static const double DRAPE_BAKE_TIME_BUDGET_STILL = 60.0; // ms, camera at rest
                    // The moving budget outlives the move by this. debug.massif.drapesettle <ms>
                    // overrides it for an A/B (0 = the at-rest budget the frame the camera stops).
                    static const double DRAPE_BAKE_SETTLE_MS = [] {
                        double settle = 300.0;
#ifdef __ANDROID__
                        char property[PROP_VALUE_MAX] = { 0 };
                        if (__system_property_get("debug.massif.drapesettle", property) > 0) {
                            settle = std::atof(property);
                        }
#endif
                        return settle;
                    }();
                    struct BakeRequest { vt::TileId tileId; std::size_t fingerprint; std::size_t drapedIndex; };
                    std::vector<BakeRequest> blankTiles, standInTiles, partialTiles, staleTiles, restackTiles;

                    // No elevation for this tile: stand on the previous generation rather than draw
                    // it flat at sea level, and draw nothing if there is no stand-in - a false
                    // ground writes depth and hides what is behind it. Flat stays only when NOTHING
                    // has elevation. See docs/internals/rendering/04-terrain.md, "Stand-ins".
                    int displacedLeaves = 0;
                    for (auto it = drapeTiles.begin(); it != drapeTiles.end(); it++) {
                        displacedLeaves += leafElevation[it->first] ? 1 : 0;
                    }
                    bool sceneDisplaced = displacedLeaves > 0;

                    // SEEDING: copy the cached tiles covering this ground into the new texture (a
                    // few quads, not a bake) so it shows the map from the frame it appears. Seeds
                    // are never sources, so nothing degrades through repeated copying.
                    static const int DRAPE_SEED_BUDGET = 16;
                    int seedBudget = DRAPE_SEED_BUDGET;
                    int seededTiles = 0;
                    struct SeedSource { unsigned int texture; float dstX, dstY, dstScale, uvX, uvY, uvScale; };
                    auto seedTile = [&](const vt::TileId& tileId, unsigned int texture) {
                        if (seedBudget <= 0 || texture == 0) {
                            return false;
                        }
                        std::vector<SeedSource> sources;
                        // Finer tiles first: they are the ones just replaced, at full detail, and
                        // together they tile this one exactly.
                        std::function<void(const vt::TileId&, int)> collectDescendants = [&](const vt::TileId& parent, int depth) {
                            for (int dy = 0; dy < 2; dy++) {
                                for (int dx = 0; dx < 2; dx++) {
                                    vt::TileId child = parent.getChild(dx, dy);
                                    unsigned int childTexture = _terrainDrapeCache->findBaked(child, 0);
                                    if (childTexture != 0) {
                                        int levels = child.zoom - tileId.zoom;
                                        int span = 1 << levels;
                                        int ix = child.x - (tileId.x << levels);
                                        int iy = child.y - (tileId.y << levels);
                                        // Mirrored y: texture v runs north, the XYZ tile y runs south.
                                        sources.push_back(SeedSource { childTexture, static_cast<float>(ix) / span, static_cast<float>(span - 1 - iy) / span, 1.0f / span, 0.0f, 0.0f, 1.0f });
                                    } else if (depth > 0) {
                                        collectDescendants(child, depth - 1);
                                    }
                                }
                            }
                        };
                        collectDescendants(tileId, 2);
                        if (sources.empty()) {
                            vt::TileId ancestor = tileId;
                            float offsetX = 0.0f, offsetY = 0.0f, scale = 1.0f;
                            for (int level = 0; level < 6 && ancestor.zoom > 0; level++) {
                                int childX = ancestor.x & 1;
                                int childY = 1 - (ancestor.y & 1);
                                ancestor = vt::TileId(ancestor.zoom - 1, ancestor.x >> 1, ancestor.y >> 1);
                                scale *= 0.5f;
                                offsetX = offsetX * 0.5f + childX * 0.5f;
                                offsetY = offsetY * 0.5f + childY * 0.5f;
                                unsigned int ancestorTexture = _terrainDrapeCache->findBaked(ancestor, 0);
                                if (ancestorTexture != 0) {
                                    sources.push_back(SeedSource { ancestorTexture, 0.0f, 0.0f, 1.0f, offsetX, offsetY, scale });
                                    break;
                                }
                            }
                        }
                        if (sources.empty()) {
                            return false; // genuinely new ground: nothing in the cache covers it
                        }
                        beginOffscreen();
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
                        glClearColor(drapeClearColor.getR() / 255.0f, drapeClearColor.getG() / 255.0f, drapeClearColor.getB() / 255.0f, drapeClearColor.getA() / 255.0f);
                        glClear(GL_COLOR_BUFFER_BIT);
                        for (const SeedSource& source : sources) {
                            drapeLayers.front()->blitDrapeTexture(source.texture, source.dstX, source.dstY, source.dstScale, source.uvX, source.uvY, source.uvScale);
                        }
                        seedBudget--;
                        seededTiles++;
                        TerrainDrapeCache::generateMipmaps(texture);
                        return true;
                    };

                    for (auto it = drapeTiles.begin(); it != drapeTiles.end(); it++) {
                        bool needsBake = false;
                        bool hasContent = false;
                        unsigned int texture = _terrainDrapeCache->acquire(it->first, 0, it->second, needsBake, hasContent);
                        if (!hasContent && seedTile(it->first, texture)) {
                            _terrainDrapeCache->markSeeded(it->first, 0);
                            hasContent = true;
                        }
                        bool baked = _terrainDrapeCache->isBaked(it->first, 0);
                        // "Has a texture" is not "shows the map": the first bake after a zoom out
                        // holds only the raster layers, and replacing the previous generation with
                        // that is THE flash. Usable = every layer that has something is in it.
                        std::size_t wantedMask = drapeTileLayerMasks[it->first];
                        std::size_t bakedMask = _terrainDrapeCache->bakedLayerMask(it->first, 0);
                        bool complete = baked && (wantedMask & ~bakedMask) == 0;
                        // A seed already IS the finer generation, composited into this tile's own
                        // texture, so nothing has to be drawn over it.
                        bool showsStandIn = hasContent && !baked;
                        // A tile with no content yet must not be sampled: its texture came from
                        // the recycle pool and still holds another tile's picture. Stand in on the
                        // nearest baked ancestor instead - a flat fill here is a white block, and
                        // during a zoom a whole screen of them flashes on and off.
                        DrapedTile draped { it->first, hasContent ? texture : 0u, 0.0f, 0.0f, 1.0f };
                        if (!hasContent) {
                            vt::TileId ancestor = it->first;
                            float offsetX = 0.0f, offsetY = 0.0f, scale = 1.0f;
                            for (int level = 0; level < 6 && ancestor.zoom > 0; level++) {
                                // Mirror the y index: tile-local y runs northward while the XYZ
                                // tile y runs southward (same convention as the bake sub-rect).
                                int childX = ancestor.x & 1;
                                int childY = 1 - (ancestor.y & 1);
                                ancestor = vt::TileId(ancestor.zoom - 1, ancestor.x >> 1, ancestor.y >> 1);
                                scale *= 0.5f;
                                offsetX = offsetX * 0.5f + childX * 0.5f;
                                offsetY = offsetY * 0.5f + childY * 0.5f;
                                unsigned int ancestorTexture = _terrainDrapeCache->findBaked(ancestor, 0);
                                if (ancestorTexture != 0) {
                                    draped.texture = ancestorTexture;
                                    draped.uvOffsetX = offsetX;
                                    draped.uvOffsetY = offsetY;
                                    draped.uvScale = scale;
                                    break;
                                }
                            }
                        }
                        // No DEM for this tile while the rest of the scene is displaced: its own
                        // surface would be the false flat ground, so it is not drawn at all. Its
                        // descendants still are - they are the generation that HAS elevation.
                        bool skipSurface = sceneDisplaced && !leafElevation[it->first];
                        std::size_t drapedIndex = std::numeric_limits<std::size_t>::max(); // never indexes the list
                        if (!skipSurface) {
                            drapedIndex = drapedTiles.size(); // before the stand-in draws below extend the list
                            drapedTiles.push_back(draped);
                        } else {
                            skippedSurfaces++;
                        }
                        // An ancestor sub-rect already covers a contentless tile, and it is the
                        // better stand-in: the descendants are separate surfaces at a finer tile
                        // zoom, so their meshes coincide with this leaf's but are not the same
                        // triangles and drawn over it they read as tiles sitting slightly off the
                        // terrain - while the ancestor is the SAME mesh with a blurrier texture,
                        // which merely looks soft. Prefer the soft one. A tile whose own surface
                        // is skipped has no ancestor draw at all, so it still needs them.
                        bool showsAncestor = !hasContent && draped.texture != 0 && !skipSurface;
                        // Same rule for a leaf drawing its OWN bake: it already covers this ground
                        // and is merely missing a layer, with the re-bake queued. Stacking the finer
                        // generation over it is the second tesselation the comment above warns
                        // about - measured as a two-frame mesh pop at every integer zoom out.
                        bool showsOwnBake = hasContent && baked && !skipSurface;
                        if (((!complete && !showsStandIn) || skipSurface) && !showsAncestor && !showsOwnBake) {
                            // Zooming out the cached tiles are the FINER ones underneath; draw them
                            // over the top, several levels deep. They must come AFTER this tile's own
                            // entry - the surfaces coincide and the later draw wins, so pushed first
                            // they are buried under the fill they replace (the white screen).
                            std::function<void(const vt::TileId&, int)> drawBakedDescendants = [&](const vt::TileId& tileId, int depth) {
                                for (int dy = 0; dy < 2; dy++) {
                                    for (int dx = 0; dx < 2; dx++) {
                                        vt::TileId child = tileId.getChild(dx, dy);
                                        unsigned int childTexture = _terrainDrapeCache->findBaked(child, 0);
                                        if (childTexture != 0 && sceneDisplaced && !hasElevationData(child)) {
                                            childTexture = 0; // cached picture, but no ground to put it on
                                        }
                                        if (childTexture != 0 && (wantedMask & ~_terrainDrapeCache->bakedLayerMask(child, 0)) != 0) {
                                            childTexture = 0; // as incomplete as the tile it would stand in for
                                        }
                                        if (childTexture != 0) {
                                            drapedTiles.push_back(DrapedTile { child, childTexture, 0.0f, 0.0f, 1.0f });
                                        } else if (depth > 0) {
                                            drawBakedDescendants(child, depth - 1);
                                        }
                                    }
                                }
                            };
                            drawBakedDescendants(it->first, 2);
                        }
                        // A mask evicted on its own - it is a separate cache entry - would leave the
                        // tile's live layers unmasked for as long as the colour drape stays current,
                        // which is for ever. Re-bake the tile so its masks come back with it.
                        for (std::size_t k = 0; k < drapeCuts.size() && !needsBake; k++) {
                            needsBake = !_terrainDrapeCache->isBaked(it->first, static_cast<int>(k) + 1);
                        }
                        if (!needsBake) {
                            continue;
                        }
                        BakeRequest request { it->first, it->second, drapedIndex };
                        if (baked && _terrainDrapeCache->isStale(it->first, 0)) {
                            restackTiles.push_back(request);     // shows the previous layer stack
                        } else if (baked && !complete) {
                            partialTiles.push_back(request);     // shows part of the stack: a layer is simply absent
                        } else if (baked) {
                            staleTiles.push_back(request);       // shows its own, older, picture
                        } else if (hasContent || draped.texture != 0) {
                            standInTiles.push_back(request);     // seeded, or standing in on an ancestor
                        } else {
                            blankTiles.push_back(request);       // shows a flat fill: a hole
                        }
                    }
                    // Nearest the focus first, within each class: the budget lets a few bakes
                    // through per frame, and in cover order the corner of the screen could fill
                    // before the point the user is looking at. Measured in tile lengths of the
                    // tile's own zoom, so a coarse tile at the edge does not outrank a fine one
                    // under the focus.
                    cglib::vec3<double> bakeFocus = viewState.getFocusPos();
                    auto focusDistance = [focusX = bakeFocus(0) / Const::WORLD_SIZE, focusY = bakeFocus(1) / Const::WORLD_SIZE](const vt::TileId& tileId) {
                        double extent = static_cast<double>(1 << tileId.zoom);
                        double dx = (tileId.x + 0.5) / extent - 0.5 - focusX;
                        double dy = 0.5 - (tileId.y + 0.5) / extent - focusY;
                        return (dx * dx + dy * dy) * extent * extent;
                    };
                    {
                        auto nearestFirst = [&focusDistance](std::vector<BakeRequest>& requests) {
                            std::stable_sort(requests.begin(), requests.end(), [&focusDistance](const BakeRequest& a, const BakeRequest& b) {
                                return focusDistance(a.tileId) < focusDistance(b.tileId);
                            });
                        };
                        nearestFirst(blankTiles);
                        nearestFirst(restackTiles);
                        nearestFirst(standInTiles);
                        nearestFirst(partialTiles);
                        nearestFirst(staleTiles);
                    }
                    // The tiles that carry a bridge or a tunnel, and nothing else. A map with no
                    // spans leaves this empty, so it pays for no texture, no bake and no lookup.
                    // Negative: the cache reads stack > 0 as a one-channel mask, 0 as the ground's
                    // colour drape, so a negative index is another COLOUR drape without touching it.
                    const int SPAN_DRAPE_STACK = -1;
                    std::map<vt::TileId, std::size_t> spanDrapeTiles;
                    for (std::size_t i = 0; i < drapeLayers.size(); i++) {
                        drapeLayers[i]->collectSpanDrapeTiles(spanDrapeTiles);
                    }
                    auto bakeTile = [&](const BakeRequest& request) {
                        bool needsBake = false, hasContent = false;
                        unsigned int texture = _terrainDrapeCache->acquire(request.tileId, 0, request.fingerprint, needsBake, hasContent);
                        beginOffscreen();
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
                        glClearColor(drapeClearColor.getR() / 255.0f, drapeClearColor.getG() / 255.0f, drapeClearColor.getB() / 255.0f, drapeClearColor.getA() / 255.0f);
                        glClear(GL_COLOR_BUFFER_BIT);
                        // Layer order matters: later layers composite over earlier ones, which is
                        // why the owner clears and the bakers do not.
                        std::size_t bakedMask = 0;
                        for (std::size_t i = 0; i < drapeLayers.size(); i++) {
                            int primitives = drapeLayers[i]->bakeDrapeTile(request.tileId);
                            bakedPrimitives += primitives;
                            if (primitives > 0 && i < sizeof(std::size_t) * 8) {
                                bakedMask |= static_cast<std::size_t>(1) << i;
                            }
                        }
                        // What went in, not what was asked for: a layer that turned out to have
                        // nothing stays missing from the mask, so the tile is re-baked as soon as
                        // that layer does have something.
                        // A terrain paint is not in that mask (it reports no tiles), so a tile it
                        // could not paint - its elevation texture was still being prepared - would
                        // otherwise keep its unshaded picture for as long as it stays cached. Mark
                        // such a tile with a fingerprint that cannot match, which makes it STALE:
                        // it is drawn, with what it has, and baked again at the first opportunity.
                        std::size_t bakedFingerprint = request.fingerprint;
                        for (std::size_t i = 0; i < drapeLayers.size() && i < sizeof(std::size_t) * 8; i++) {
                            if (drapeLayers[i]->paintsEveryDrapeTile() && (bakedMask & (static_cast<std::size_t>(1) << i)) == 0) {
                                // ONLY when the paint could have painted this tile and the texture
                                // merely was not uploaded yet. A tile that can NEVER be painted would
                                // otherwise stay stale for ever, re-baked one per frame.
                                auto leafElevationIt = leafElevation.find(request.tileId);
                                if (leafElevationIt != leafElevation.end() && leafElevationIt->second) {
                                    bakedFingerprint = ~request.fingerprint;
                                }
                                break;
                            }
                        }
                        // The extrusions' contact shadows, resolved per tile under MIN and
                        // multiplied into this tile's drape. Baked into the ground itself, so the
                        // shadow follows the terrain exactly - a screen-space capsule cannot,
                        // because one quad spans a slope linearly between its own corners.
                        //
                        // Every GL state this touches is put back the way bakeDrapeTile leaves it.
                        // Culling above all: the bake matrix maps tile-local xy straight to clip
                        // with no y flip, so a stray glEnable(GL_CULL_FACE) here empties every tile
                        // baked after this one.
                        if (groundAOWanted) {
                            if (!_groundAODrapeBuffer) {
                                _groundAODrapeBuffer = std::make_unique<ScreenMaskBuffer>(false);
                            }
                            _groundAODrapeBuffer->setSize(resolution, resolution, 1);
                            GLint drapeFBO = 0;
                            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &drapeFBO);
                            int aoBaked = 0;
                            if (_groundAODrapeBuffer->beginPassRaw()) {
                                glEnable(GL_BLEND);
                                glBlendFunc(GL_ONE, GL_ONE);
                                glBlendEquation(GL_MIN);
                                for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                                    aoBaked += tileLayer->bakeGroundAOMask(request.tileId);
                                }
                                glBlendEquation(GL_FUNC_ADD);
                                _groundAODrapeBuffer->endPassRaw(drapeFBO, resolution, resolution);
                                if (aoBaked > 0) {
                                    // The drape colour is PREMULTIPLIED, so scaling rgb alone is
                                    // valid; the mask's own alpha is 1, so dst alpha is untouched.
                                    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
                                    drawMaskQuad(_groundAODrapeBuffer->getTexture(), 1.0f / resolution, 1.0f / resolution);
                                }
                                // Back to exactly what bakeDrapeTile establishes and the next tile
                                // relies on (beginOffscreen only runs once per frame).
                                glDisable(GL_CULL_FACE);
                                glDisable(GL_DEPTH_TEST);
                                glDepthMask(GL_FALSE);
                                glDisable(GL_STENCIL_TEST);
                                glEnable(GL_BLEND);
                                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                            }
                        }
                        _terrainDrapeCache->markBaked(request.tileId, 0, bakedFingerprint, bakedMask);
                        TerrainDrapeCache::generateMipmaps(texture);
                        // The occlusion masks of this tile (#175), in the same pass and off the same
                        // fingerprint, so a mask can never describe a different generation of the
                        // map than the drape it is read beside. Stack 1+k, R8.
                        for (std::size_t k = 0; k < drapeCuts.size(); k++) {
                            std::size_t maskFingerprint = bakedFingerprint ^ (drapeCutSignature + k * 0x9e3779b9);
                            bool maskNeedsBake = false, maskHasContent = false;
                            unsigned int maskTexture = _terrainDrapeCache->acquire(request.tileId, static_cast<int>(k) + 1, maskFingerprint, maskNeedsBake, maskHasContent);
                            if (maskTexture == 0) {
                                continue;
                            }
                            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskTexture, 0);
                            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                            glClear(GL_COLOR_BUFFER_BIT);
                            for (std::size_t i = drapeCuts[k].layerIndex; i < drapeLayers.size(); i++) {
                                // The cut's own layer starts at the cut; every later layer is wholly
                                // above it.
                                int fromStyleLayerIdx = (i == drapeCuts[k].layerIndex ? drapeCuts[k].styleLayerIdx : std::numeric_limits<int>::min());
                                drapeLayers[i]->bakeDrapeCoverage(request.tileId, fromStyleLayerIdx);
                            }
                            _terrainDrapeCache->markBaked(request.tileId, static_cast<int>(k) + 1, maskFingerprint, 0);
                            TerrainDrapeCache::generateMipmaps(maskTexture);
                        }
                        bakedTiles++;
                        bakedThisFrame++;
                        VT_STAT_INC(drapeBakes);
                        if (request.drapedIndex < drapedTiles.size()) {
                            drapedTiles[request.drapedIndex] = DrapedTile { request.tileId, texture, 0.0f, 0.0f, 1.0f }; // baked now, safe to sample
                        }
                    };
                    // The counts above say how URGENT each class is; how many of them a frame can
                    // actually afford is a time question, and the answer is not the same on two
                    // devices: the same bake was measured at ~2 ms on the emulator and 25+ ms on an
                    // Adreno 610, so eight of them are a hiccup on one and a third of a second on
                    // the other. Bake in priority order until the frame's bake time is spent (one
                    // bake always goes through, or a slow device would never fill a tile in).
                    // Same "did the camera move since the previous frame" test the occlusion
                    // read-back throttle uses (TerrainRenderer::updateDepthBuffer).
                    const cglib::mat4x4<double>& bakeMVPMatrix = viewState.getModelviewProjectionMat();
                    std::chrono::steady_clock::time_point bakeNow = std::chrono::steady_clock::now();
                    if (!(_drapeBakeLastMVPMatrix == bakeMVPMatrix)) {
                        _drapeBakeLastMoveTime = bakeNow;
                    }
                    // ...and for a settle window past the last move: a fast zoom is a chain of
                    // gestures with rests of a few frames between them, and opening the at-rest
                    // budget in each rest made every one a 60 ms frame while the new zoom's cover
                    // baked - the map hung between the user's fingers. Watched at Paris, z15-18
                    // fast: 100-144 bakes queued, 39 frames over 100 ms, the worst 375 ms.
                    bool bakeCameraMoving = std::chrono::duration<double, std::milli>(bakeNow - _drapeBakeLastMoveTime).count() < DRAPE_BAKE_SETTLE_MS;
                    _drapeBakeLastMVPMatrix = bakeMVPMatrix;
                    // Settled: adopt this zoom for the drape. The fingerprint above reads this, so
                    // the tiles go stale on the frame the gesture ends and are re-baked from there.
                    if (!bakeCameraMoving) {
                        std::size_t zoomTerm = DrapeTuning::bakeZoomTerm(viewState.getZoom(), DRAPE_REBAKE_ZOOM_THRESHOLD);
                        if (zoomTerm != _drapeBakeZoomTerm) {
                            _drapeBakeZoomTerm = zoomTerm;
                            requestRedraw(); // nothing else asks for the frame the re-bake happens in
                        }
                    }
                    double bakeTimeBudget = (bakeCameraMoving ? DRAPE_BAKE_TIME_BUDGET : DRAPE_BAKE_TIME_BUDGET_STILL);
                    std::chrono::steady_clock::time_point bakeStart = std::chrono::steady_clock::now();
                    VT_STAT_ADD(drapeBakeQueued, static_cast<long long>(blankTiles.size() + restackTiles.size() + standInTiles.size() + partialTiles.size() + staleTiles.size()));
                    auto bakeTimeLeft = [&bakeStart, &bakedThisFrame, bakeTimeBudget]() {
                        if (bakedThisFrame == 0) {
                            return true; // always make progress
                        }
                        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bakeStart).count() < bakeTimeBudget;
                    };
                    int budget = DRAPE_BAKE_BUDGET_BLANK;
                    for (auto it = blankTiles.begin(); it != blankTiles.end() && budget > 0 && bakeTimeLeft(); it++, budget--) {
                        bakeTile(*it);
                    }
                    budget = DRAPE_BAKE_BUDGET_RESTACK;
                    for (auto it = restackTiles.begin(); it != restackTiles.end() && budget > 0 && bakeTimeLeft(); it++, budget--) {
                        bakeTile(*it);
                    }
                    budget = DRAPE_BAKE_BUDGET_STANDIN;
                    for (auto it = standInTiles.begin(); it != standInTiles.end() && budget > 0 && bakeTimeLeft(); it++, budget--) {
                        bakeTile(*it);
                    }
                    budget = DRAPE_BAKE_BUDGET_PARTIAL;
                    for (auto it = partialTiles.begin(); it != partialTiles.end() && budget > 0 && bakeTimeLeft(); it++, budget--) {
                        bakeTile(*it);
                    }
                    // One stale tile per frame is the right ration while the camera moves - the
                    // picture is merely out of date and the frame has better things to do. On a map
                    // at REST it is a livelock: nothing else asks for frames, so the map draws only
                    // the frames the bakes themselves request, and a backlog of a few dozen tiles
                    // takes half a minute to drain, one texture swap at a time - which is what the
                    // terrain "loading and blinking" looks like when nothing is moving. At rest let
                    // the wall-clock budget ration it instead: a longer frame is invisible on a map
                    // that is not moving, the same argument DRAPE_BAKE_TIME_BUDGET_STILL is made of.
                    budget = (bakeCameraMoving ? DRAPE_BAKE_BUDGET_STALE : DRAPE_BAKE_BUDGET_BLANK);
                    for (auto it = staleTiles.begin(); it != staleTiles.end() && budget > 0 && bakeTimeLeft(); it++, budget--) {
                        bakeTile(*it);
                    }
                    // Baking is rationed over several frames, so it only finishes if those frames
                    // happen. Nothing else asks for them: the tile arrived, its layer redrew once,
                    // and the budget took the first few tiles. On a map that now goes idle the rest
                    // of the queue simply stopped - a layer switched on (a hillshade, a raster)
                    // appeared a few tiles at a time and only while the user kept panning. Keep
                    // asking for frames while there is baking left to do, and stop when there is not.
                    if (blankTiles.size() > DRAPE_BAKE_BUDGET_BLANK
                        || standInTiles.size() > DRAPE_BAKE_BUDGET_STANDIN
                        || partialTiles.size() > DRAPE_BAKE_BUDGET_PARTIAL
                        || staleTiles.size() > DRAPE_BAKE_BUDGET_STALE) {
                        requestRedraw();
                    }
                    // The DECK's own drape, baked per RENDER tile rather than per drape leaf: the
                    // deck is drawn with its render tile, and one draw cannot sample several
                    // textures - the same limit the coverage masks carry. Its own small loop, run
                    // only when something in view actually has a span.
                    if (!spanDrapeTiles.empty()) {
                        std::map<vt::TileId, unsigned int> spanDrapeTextures;
                        beginOffscreen();
                        // Under the frame's bake budget like every other class, nearest the focus
                        // first, and one always goes through. Unbudgeted, an integer zoom renamed
                        // every bridge tile in view and baked them all in one frame - 150-210 ms of
                        // it at Paris, the frame the user feels at each zoom level. A deck whose
                        // drape is not baked yet draws its plain roof for those frames and asks
                        // for the next.
                        std::vector<std::pair<vt::TileId, std::size_t>> spanBakeOrder(spanDrapeTiles.begin(), spanDrapeTiles.end());
                        std::stable_sort(spanBakeOrder.begin(), spanBakeOrder.end(), [&focusDistance](const std::pair<vt::TileId, std::size_t>& a, const std::pair<vt::TileId, std::size_t>& b) {
                            return focusDistance(a.first) < focusDistance(b.first);
                        });
                        int spanBakedThisFrame = 0;
                        bool spanBakesLeft = false;
                        for (auto it = spanBakeOrder.begin(); it != spanBakeOrder.end(); it++) {
                            bool spanNeedsBake = false, spanHasContent = false;
                            unsigned int spanTexture = _terrainDrapeCache->acquire(it->first, SPAN_DRAPE_STACK, it->second, spanNeedsBake, spanHasContent);
                            if (spanTexture == 0) {
                                continue;
                            }
                            if (spanNeedsBake && spanBakedThisFrame > 0 && !bakeTimeLeft()) {
                                spanBakesLeft = true;
                                continue; // not handed over: an unbaked texture is not a drape
                            }
                            if (spanNeedsBake) {
                                spanBakedThisFrame++;
                                bakedThisFrame++;
                                VT_STAT_INC(drapeBakes);
                                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, spanTexture, 0);
                                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                                glClear(GL_COLOR_BUFFER_BIT);
                                for (std::size_t i = 0; i < drapeLayers.size(); i++) {
                                    drapeLayers[i]->bakeSpanDrapeTile(it->first);
                                }
                                _terrainDrapeCache->markBaked(it->first, SPAN_DRAPE_STACK, it->second, 0);
                                TerrainDrapeCache::generateMipmaps(spanTexture);
                            }
                            spanDrapeTextures[it->first] = spanTexture;
                        }
                        for (std::size_t i = 0; i < drapeLayers.size(); i++) {
                            drapeLayers[i]->setSpanDrapeTextures(spanDrapeTextures);
                        }
                        if (spanBakesLeft) {
                            requestRedraw();
                        }
                    }
                    VT_STAT_ADD(drapeBakeNs, std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - bakeStart).count());
                    if (bakeStarted) {
                        // Detach before sampling: a texture left attached to a framebuffer counts
                        // as a render target, and sampling it in the same frame is undefined - on
                        // the emulator every drape texture then reads back black.
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
                        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
                        glViewport(0, 0, viewState.getWidth(), viewState.getHeight());
                    }

                    // Hand the masks to the layers BEFORE they draw, the same explicit per-frame
                    // hand-off setExternalDrapeTiles is. A tile whose mask has not been baked yet -
                    // budgeted out, or its drape only just seeded - is absent from the map, and its
                    // live layers draw unmasked for those frames, as they did before #175.
                    std::vector<std::map<vt::TileId, unsigned int> > drapeCoverageMasks(drapeCuts.size());
                    for (std::size_t k = 0; k < drapeCuts.size(); k++) {
                        for (const vt::TileId& tileId : drapeTileIds) {
                            unsigned int maskTexture = _terrainDrapeCache->findBaked(tileId, static_cast<int>(k) + 1);
                            if (maskTexture != 0) {
                                drapeCoverageMasks[k][tileId] = maskTexture;
                            }
                        }
                    }
                    for (std::size_t i = 0; i < drapeLayers.size(); i++) {
                        drapeLayers[i]->setDrapeCoverageMasks(drapeCoverageMasks, drapeLayerMasks[i]);
                    }

                    // Directional shadows over the drape cover.
                    ResolvedLighting lighting;
                    std::array<double, TerrainShadowMap::MAX_CASCADES> shadowTexelMeters = { };
                    applyTerrainShadows(drapeLayers, drapeTileIds, terrainOptions, viewState, prevFBO, bakedThisFrame > 0, true, lighting, shadowTexelMeters);

                    // The shared surface is the only depth-writing terrain geometry.
                    // GL_LEQUAL, not the default GL_LESS: the global terrain background drawn
                    // just above uses the SAME meshes and has already written their depth, so a
                    // GL_LESS surface draw is rejected everywhere and the drape never reaches
                    // the screen - the terrain then shows the background colour and nothing else.
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests
                    groundAODraped = groundAODraped || !drapedTiles.empty();
                    for (auto it = drapedTiles.begin(); it != drapedTiles.end(); it++) {
                        // Every drape tile gets a surface, always. The surface is the terrain's
                        // only depth writer, so a tile skipped because its bake has not landed
                        // yet leaves a depth hole - and vector elements and billboards behind the
                        // terrain there pop into view for exactly those frames.
                        if (it->texture != 0) {
                            surfaceDraws += drapeLayers.front()->renderDrapedSurface(it->tileId, it->texture, it->uvOffsetX, it->uvOffsetY, it->uvScale);
                        } else {
                            surfaceDraws += drapeLayers.front()->renderDrapedSurfaceFill(it->tileId, drapeClearColor);
                            filledSurfaces++;
                        }
                    }
                    glEnable(GL_CULL_FACE);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_FALSE);
                    _terrainDrapeCache->endFrame();

                    // Ground with nothing on it is the one failure the user sees immediately, and
                    // it has two quite different causes - a tile drawn in the flat clear colour,
                    // or a tile not drawn at all because it has no elevation yet. Log the frame it
                    // happens in, with the state that produced it, rather than in the periodic
                    // dump that a half-second flash never coincides with.
                    if (filledSurfaces > 0 || skippedSurfaces > 0) {
                        static int emptyGroundFrame = 0, lastEmptyGroundLog = -1000;
                        emptyGroundFrame++;
                        if (emptyGroundFrame - lastEmptyGroundLog > 30) {
                            lastEmptyGroundLog = emptyGroundFrame;
                            Log::Infof("MapRenderer: RTT drape EMPTY GROUND - %d flat fills, %d tiles skipped for missing elevation, of %d drawn (%d leaves, split level %d, camera zoom %.2f); seeded %d, blank %d, stand-in %d, partial %d, stale %d",
                                filledSurfaces, skippedSurfaces, static_cast<int>(drapedTiles.size()),
                                static_cast<int>(drapeTiles.size()), drapeZoom, viewState.getZoom(), seededTiles,
                                static_cast<int>(blankTiles.size()), static_cast<int>(standInTiles.size()),
                                static_cast<int>(partialTiles.size()), static_cast<int>(staleTiles.size()));
                        }
                    }

                    // One-time state dump: confirms whether the RTT path is actually live, and
                    // with how many layers/tiles, rather than being inferred from symptoms.
                    static double drapeMsSum = 0;
                    static double drapeMsMax = 0;
                    static int drapeMsCount = 0;
                    double drapeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - drapeStart).count();
                    FRAME_PROF_SET(drapeMs, drapeMs);
                    FRAME_PROF_GPU_END();
                    drapeMsSum += drapeMs;
                    drapeMsMax = std::max(drapeMsMax, drapeMs);
                    drapeMsCount++;
                    static int drapeStateFrame = 0;
                    if ((drapeStateFrame++ % 60) == 0 && drapedTiles.size() > 0) {
                        Log::Infof("MapRenderer: RTT drape cost avg %.1f ms, max %.1f ms over %d frames", drapeMsSum / std::max(1, drapeMsCount), drapeMsMax, drapeMsCount);
                        drapeMsSum = 0; drapeMsMax = 0; drapeMsCount = 0;
                    }
                    if ((drapeStateFrame % 600) == 1 && drapedTiles.size() > 0) {
                        int minZoom = 99, maxZoom = -1;
                        for (auto it2 = drapedTiles.begin(); it2 != drapedTiles.end(); it2++) {
                            minZoom = std::min(minZoom, it2->tileId.zoom);
                            maxZoom = std::max(maxZoom, it2->tileId.zoom);
                        }
                        Log::Infof("MapRenderer: RTT drape tiles zoom %d..%d, count %d", minZoom, maxZoom, static_cast<int>(drapedTiles.size()));
                        // Queue sizes say which of the four states the cover is actually in - a
                        // standing 'partial' backlog means the bake never catches up with the
                        // layers, which looks like the whole map stuck on bare hillshade.
                        Log::Infof("MapRenderer: RTT drape cover - split level %d (collected up to %d, camera zoom %.2f), leaves %d",
                            drapeZoom, maxCollectedZoom, viewState.getZoom(), static_cast<int>(drapeTiles.size()));
                        Log::Infof("MapRenderer: RTT drape seeded %d tiles from cache this frame", seededTiles);
                        Log::Infof("MapRenderer: RTT drape queues - blank %d, stand-in %d, partial %d, stale %d, tiles without elevation %d of %d",
                            static_cast<int>(blankTiles.size()), static_cast<int>(standInTiles.size()),
                            static_cast<int>(partialTiles.size()), static_cast<int>(staleTiles.size()),
                            static_cast<int>(drapeTiles.size()) - displacedLeaves, static_cast<int>(drapeTiles.size()));
                        Log::Infof("MapRenderer: RTT drape ACTIVE - layers %d, collected tiles %d, drawn tiles %d, resolution %d, baked %d tiles / %d primitives, surface draws %d (%d unbaked fills)",
                            static_cast<int>(drapeLayers.size()), static_cast<int>(collectedTiles.size()),
                            static_cast<int>(drapedTiles.size()), resolution, bakedTiles, bakedPrimitives, surfaceDraws, filledSurfaces);
                        Log::Infof("MapRenderer: shadow caster passes %d over %d frames, %d cascades, %d caster tiles per pass, %.1f ms per pass, %d extrusion draws per pass, %d casters skipped for missing elevation per pass, texels per cascade %.1f/%.1f/%.1f/%.1f m (camera zoom %.2f tilt %.1f)", shadowPasses, drapeStateFrame, _shadowMapCascades, shadowCasterDraws / std::max(1, shadowPasses), shadowMsSum / std::max(1, shadowPasses), shadowExtrusionDraws / std::max(1, shadowPasses), shadowCastersNoElevation / std::max(1, shadowPasses), shadowTexelMeters[0], shadowTexelMeters[1], shadowTexelMeters[2], shadowTexelMeters[3], viewState.getZoom(), viewState.getTilt());
                    }
                    }
                    catch (const std::exception& ex) {
                        // A shader that fails to compile or link throws from the render thread.
                        // Losing the drape is bad; taking the process down with it is worse.
                        Log::Errorf("MapRenderer: RTT drape failed: %s", ex.what());
                        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
                        glViewport(0, 0, viewState.getWidth(), viewState.getHeight());
                    }
                }
            }
        }
        if (drapeLayers.empty() && !sharedGroundActive) {
            std::vector<std::shared_ptr<TileLayer> > allTileLayers;
            for (const std::shared_ptr<Layer>& layer : layers) {
                layer->collectDrapeLayers(allTileLayers, viewState);
            }
            for (const std::shared_ptr<TileLayer>& tileLayer : allTileLayers) {
                tileLayer->setExternalDrapeTarget(false);
                // No shared ground either (terrain off, or a stack with no drapeable layer):
                // release the cover so a layer left holding one from a terrain frame does not
                // keep suppressing its own depth pre-pass and drawing on tiles nobody covers.
                tileLayer->setTerrainGroundTiles(std::vector<vt::TileId>(), std::vector<int>());
            }
            if (terrainMode) {
                static bool noDrapeLogged = false;
                if (!noDrapeLogged) {
                    noDrapeLogged = true;
                    Log::Info("MapRenderer: neither the RTT drape nor a shared ground is active in terrain mode - falling back to the per-layer depth path");
                }
            }
        }

        // Create new billboard sorter instance
        std::vector<std::shared_ptr<BillboardDrawData> > billboardDrawDatas;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            billboardDrawDatas.reserve(_billboardDrawDatas.size());
        }
        BillboardSorter billboardSorter(billboardDrawDatas);

        // Do base drawing pass
        bool needRedraw = false;
        FRAME_PROF_NOW(profLayerStart);
        FRAME_PROF_GPU_BEGIN(SECTION_LAYERS);
        unsigned int redrawMask = 0; // which layer asked, so a map that never settles can be traced
        for (std::size_t i = 0; i < layers.size(); i++) {
            const std::shared_ptr<Layer>& layer = layers[i];
            if (viewState.getHorizontalLayerOffsetDir() != 0) {
                layer->offsetLayerHorizontally(viewState.getHorizontalLayerOffsetDir() * Const::WORLD_SIZE);
            }

            if (layer->onDrawFrame(deltaSeconds, billboardSorter, viewState)) {
                needRedraw = true;
                redrawMask |= 1u << std::min<std::size_t>(i, 15);
            }
        }

        FRAME_PROF_ADD(layerMs, profLayerStart);

        // Resolve the extrusions' contact shadows into one screen-space mask, under MIN blending.
        // Only when nothing is draped - with a drape it is baked into the ground instead, which is
        // the one way it follows the terrain exactly.
        // The capsule quads of a corner, of a building and its building:parts, and of two
        // neighbours all overlap; multiplied straight into the frame every one of those overlaps
        // compounds towards black. The 3D pass below only multiplies this resolved value in.
        // After the 2D pass, because that is what left each layer's render tiles current.
        {
            std::vector<std::shared_ptr<TileLayer> > aoTileLayers;
            for (const std::shared_ptr<Layer>& layer : layers) {
                layer->collectDrapeLayers(aoTileLayers, viewState);
            }
            auto aoActive = [](const std::shared_ptr<TileLayer>& tileLayer) { return tileLayer->isGroundAOActive(); };
            if (!groundAODraped && std::any_of(aoTileLayers.begin(), aoTileLayers.end(), aoActive) && viewState.getWidth() > 0 && viewState.getHeight() > 0) {
                if (!_groundAOMaskBuffer) {
                    _groundAOMaskBuffer = std::make_unique<ScreenMaskBuffer>(true);
                }
                _groundAOMaskBuffer->setSize(viewState.getWidth(), viewState.getHeight(), GROUND_AO_MASK_DIVISOR);
                GLint aoPrevFBO = 0;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &aoPrevFBO);
                FRAME_PROF_GPU_BEGIN(SECTION_GROUNDAO);
                int aoDraws = 0;
                if (_groundAOMaskBuffer->beginPass()) {
                    for (const std::shared_ptr<TileLayer>& tileLayer : aoTileLayers) {
                        aoDraws += tileLayer->renderGroundAOMask();
                    }
                    _groundAOMaskBuffer->endPass(aoPrevFBO, viewState.getWidth(), viewState.getHeight());
                }
                // ONE multiply over the whole frame, before the extrusions are drawn: they then
                // cover their own footprints, and the ground keeps the shadow around them. Drawing
                // the quads again to composite them would multiply at every overlap and undo the
                // MIN that was the point of the mask.
                if (aoDraws > 0) {
                    multiplyScreenMask(_groundAOMaskBuffer->getTexture(), 1.0f / viewState.getWidth(), 1.0f / viewState.getHeight());
                }
                FRAME_PROF_GPU_END();
            }
        }

        // Label occlusion against the 3D content, on mapbox's model (see labelVsh): a label asks
        // whether its ANCHOR is behind a building and fades out as a whole, rather than being cut
        // by one. The scene's own depth cannot answer it - it is a renderbuffer on the default
        // framebuffer - so the extrusions are drawn once more into a half-resolution depth texture
        // from the same camera. The ground is left out: labels are already tested against the
        // terrain on the CPU, per label (TileRenderer::setLabelOcclusionTest).
        //
        // Here, between the two layer passes, because the labels that sample it are drawn with the
        // 3D content (LabelRenderOrder LAST). A layer that draws its labels in the 2D pass instead
        // samples the PREVIOUS frame's buffer, which lags a moving camera.
        {
            std::vector<std::shared_ptr<TileLayer> > occlusionLayers;
            for (const std::shared_ptr<Layer>& layer : layers) {
                layer->collectDrapeLayers(occlusionLayers, viewState);
            }
            unsigned int occlusionTexture = 0;
            auto occlusionWanted = [](const std::shared_ptr<TileLayer>& tileLayer) { return tileLayer->isLabelOcclusionWanted(); };
            if (std::any_of(occlusionLayers.begin(), occlusionLayers.end(), occlusionWanted) && viewState.getWidth() > 0 && viewState.getHeight() > 0) {
                if (!_labelOcclusionBuffer) {
                    // Colour, not a depth texture: the occluders pack their window depth into rgb
                    // (the shadow caster's encoding), because sampling a depth texture from a
                    // VERTEX shader is not something every driver here does.
                    _labelOcclusionBuffer = std::make_unique<ScreenMaskBuffer>(true);
                }
                _labelOcclusionBuffer->setSize(viewState.getWidth(), viewState.getHeight(), LABEL_OCCLUSION_DIVISOR);
                GLint occlusionPrevFBO = 0;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &occlusionPrevFBO);
                int occluderDraws = 0;
                FRAME_PROF_GPU_BEGIN(SECTION_LABELOCC);
                if (_labelOcclusionBuffer->beginPass()) {
                    for (const std::shared_ptr<TileLayer>& tileLayer : occlusionLayers) {
                        occluderDraws += tileLayer->renderLabelOcclusionDepth();
                    }
                    _labelOcclusionBuffer->endPass(occlusionPrevFBO, viewState.getWidth(), viewState.getHeight());
                }
                FRAME_PROF_GPU_END();
                // An empty buffer would read as "nothing occludes anything", which is the same
                // answer as not sampling at all - and not sampling is a cheaper way to say it.
                if (occluderDraws > 0) {
                    occlusionTexture = _labelOcclusionBuffer->getTexture();
                }
            }
            for (const std::shared_ptr<TileLayer>& tileLayer : occlusionLayers) {
                tileLayer->setLabelOcclusionDepth(occlusionTexture, LABEL_OCCLUSION_SIZE_PIXELS);
            }
        }

        // Do 3D drawing pass
        FRAME_PROF_NOW(profLayer3DStart);
        FRAME_PROF_GPU_BEGIN(SECTION_LAYERS3D);
        for (std::size_t i = 0; i < layers.size(); i++) {
            if (layers[i]->onDrawFrame3D(deltaSeconds, billboardSorter, viewState)) {
                needRedraw = true;
                redrawMask |= 1u << (16 + std::min<std::size_t>(i, 15));
            }
        }

        FRAME_PROF_ADD(layer3DMs, profLayer3DStart);

        // Sort billboards, calculate rotation state
        FRAME_PROF_NOW(profBillboardStart);
        FRAME_PROF_GPU_BEGIN(SECTION_BILLBOARDS);
        billboardSorter.sort(viewState);
        
        // Draw billboards, grouped by layer renderer
        if (!billboardDrawDatas.empty()) {
            glDisable(GL_DEPTH_TEST);

            _billboardDrawDataBuffer.clear();
            std::shared_ptr<BillboardRenderer> prevRenderer;
            for (const std::shared_ptr<BillboardDrawData>& drawData : billboardDrawDatas) {
                if (std::shared_ptr<BillboardRenderer> renderer = drawData->getRenderer().lock()) {
                    if (prevRenderer && prevRenderer != renderer) {
                        prevRenderer->onDrawFrameSorted(deltaSeconds, _billboardDrawDataBuffer, viewState);
                        _billboardDrawDataBuffer.clear();
                    }
            
                    _billboardDrawDataBuffer.push_back(drawData);
                    prevRenderer = renderer;
                }
            }
            if (prevRenderer) {
                prevRenderer->onDrawFrameSorted(deltaSeconds, _billboardDrawDataBuffer, viewState);
            }

            glEnable(GL_DEPTH_TEST);
        }

        FRAME_PROF_ADD(billboardMs, profBillboardStart);
        FRAME_PROF_GPU_END();

        // Store the active billboard draw data list
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _billboardDrawDatas = std::move(billboardDrawDatas);
        }
    
        // Redraw, if needed
        if (needRedraw) {
            requestRedraw();
        }
        // A map that is standing still should stop asking for frames. When it does not - which is
        // invisible except as battery drain and a log line every 60 frames - say who is asking:
        // the low half of the mask is the base pass, the high half the 3D pass, one bit per layer.
        // If the frames keep coming but almost none of them came from a layer, the driver is an
        // external requestRedraw (a tile finishing, the elevation version moving, a camera event)
        // rather than an animation, which is a different bug with a different fix.
        {
            static int frames = 0;
            static int layerRedrawFrames = 0;
            static unsigned int redrawMaskSum = 0;
            frames++;
            if (needRedraw) {
                layerRedrawFrames++;
                redrawMaskSum |= redrawMask;
            }
            if (frames >= 300) {
                Log::Infof("MapRenderer: %d frames drawn, %d asked for by a layer, layer mask 0x%08x (low 16 bits base pass, high 16 bits 3D pass)", frames, layerRedrawFrames, redrawMaskSum);
                logRedrawSources();
                frames = 0;
                layerRedrawFrames = 0;
                redrawMaskSum = 0;
            }
        }
    }

    void MapRenderer::drawOverlayLayers(float deltaSeconds, const ViewState& viewState) {
        std::vector<std::shared_ptr<Layer> > layers = _overlayLayers;
        if (layers.empty()) {
            return;
        }

        std::vector<std::shared_ptr<BillboardDrawData> > billboardDrawDatas;
        BillboardSorter billboardSorter(billboardDrawDatas);

        bool needRedraw = false;
        for (const std::shared_ptr<Layer>& layer : layers) {
            if (viewState.getHorizontalLayerOffsetDir() != 0) {
                layer->offsetLayerHorizontally(viewState.getHorizontalLayerOffsetDir() * Const::WORLD_SIZE);
            }
            needRedraw = layer->onDrawFrame(deltaSeconds, billboardSorter, viewState) || needRedraw;
        }
        for (const std::shared_ptr<Layer>& layer : layers) {
            needRedraw = layer->onDrawFrame3D(deltaSeconds, billboardSorter, viewState) || needRedraw;
        }

        billboardSorter.sort(viewState);
        if (!billboardDrawDatas.empty()) {
            glDisable(GL_DEPTH_TEST);

            _billboardDrawDataBuffer.clear();
            std::shared_ptr<BillboardRenderer> prevRenderer;
            for (const std::shared_ptr<BillboardDrawData>& drawData : billboardDrawDatas) {
                if (std::shared_ptr<BillboardRenderer> renderer = drawData->getRenderer().lock()) {
                    if (prevRenderer && prevRenderer != renderer) {
                        prevRenderer->onDrawFrameSorted(deltaSeconds, _billboardDrawDataBuffer, viewState);
                        _billboardDrawDataBuffer.clear();
                    }
                    _billboardDrawDataBuffer.push_back(drawData);
                    prevRenderer = renderer;
                }
            }
            if (prevRenderer) {
                prevRenderer->onDrawFrameSorted(deltaSeconds, _billboardDrawDataBuffer, viewState);
            }

            glEnable(GL_DEPTH_TEST);
        }

        // The placement worker looks at one list, so these join the ones drawLayers collected
        // rather than replacing them.
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _billboardDrawDatas.insert(_billboardDrawDatas.end(), billboardDrawDatas.begin(), billboardDrawDatas.end());
        }

        if (needRedraw) {
            requestRedraw();
        }
    }

    void MapRenderer::handleRendererCaptureCallbacks() {
        int width, height;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            width = _viewState.getWidth();
            height = _viewState.getHeight();
        }
        std::shared_ptr<Bitmap> captureBitmap;
        
        std::vector<std::pair<DirectorPtr<RendererCaptureListener>, bool> > rendererCaptureListeners;
        {
            std::lock_guard<std::mutex> lock(_rendererCaptureListenersMutex);
            _rendererCaptureListeners.swap(rendererCaptureListeners);
        }

        bool callbacksPending = false;
        for (std::size_t i = 0; i < rendererCaptureListeners.size(); i++) {
            const DirectorPtr<RendererCaptureListener>& listener = rendererCaptureListeners[i].first;
            bool waitWhileUpdating = rendererCaptureListeners[i].second;
            if (waitWhileUpdating) {
                bool layersUpdating = false;
                for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
                    if (layer->isUpdateInProgress()) {
                        layersUpdating = true;
                        break;
                    }
                }
                if (_redrawPending || layersUpdating || !_cullWorker->isIdle() || !_billboardPlacementWorker->isIdle() || !_vtLabelPlacementWorker->isIdle()) {
                    std::lock_guard<std::mutex> lock(_rendererCaptureListenersMutex);
                    _rendererCaptureListeners.push_back(rendererCaptureListeners[i]);
                    callbacksPending = true;
                    continue;
                }
            }
            
            if (!captureBitmap) {
                std::vector<unsigned char> data(4 * width * height);
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, &data[0]);
                captureBitmap = std::make_shared<Bitmap>(data.data(), width, height, ColorFormat::COLOR_FORMAT_RGBA, -4 * width);
            }
            
            listener->onMapRendered(captureBitmap);
        }
        if (callbacksPending) {
            requestRedraw();
        }
    }

    MapRenderer::OptionsListener::OptionsListener(const std::shared_ptr<MapRenderer>& mapRenderer) : _mapRenderer(mapRenderer)
    {
    }

    void MapRenderer::OptionsListener::onOptionChanged(const std::string& optionName) {
        if (auto mapRenderer = _mapRenderer.lock()) {
            bool updateView = false;

            if (optionName == "AmbientLightColor" || optionName == "MainLightColor" || optionName == "MainLightDirection" || optionName == "ClearColor" || optionName == "SkyColor") {
                updateView = true;
            }
            
            if (optionName == "RenderProjectionMode" || optionName == "BaseProjection" || optionName == "ZoomRange" || optionName == "PanBounds" || optionName == "RestrictedPanning") {
                std::lock_guard<std::recursive_mutex> lock(mapRenderer->_mutex);
                mapRenderer->_viewState.calculateViewState(*mapRenderer->_options);
                mapRenderer->_viewState.clampZoom(*mapRenderer->_options);
                mapRenderer->_viewState.clampFocusPos(*mapRenderer->_options);
                updateView = true;
            }

            if (optionName == "TileDrawSize" || optionName == "DPI" || optionName == "DrawDistance" || optionName == "FieldOfViewY" || optionName == "FocusPointOffset") {
                std::lock_guard<std::recursive_mutex> lock(mapRenderer->_mutex);
                mapRenderer->_viewState.calculateViewState(*mapRenderer->_options);
                updateView = true;
            }

            if (optionName.substr(0, 14) == "TerrainOptions") {
                // Terrain changes (enabled state, exaggeration, mesh resolution, min zoom)
                // require a new cull pass so that tile layers detect the configuration change
                // and rebuild their tiles with/without terrain displacement
                updateView = true;
            }

            if (optionName.substr(0, 10) == "FogOptions") {
                // Fog reaches passes that a bare redraw does not refresh - the drape bake and the
                // terrain shadow mask are both kept until the content changes - so turning it on
                // or off left the map half updated. A cull pass refreshes all of them.
                updateView = true;
            }

            if (updateView) {
                mapRenderer->viewChanged(false, MapMoveReason::MAP_MOVE_REASON_API);
            } else {
                mapRenderer->requestRedraw();
            }
        }
    }

    const int MapRenderer::BILLBOARD_PLACEMENT_TASK_DELAY = 200;

    const int MapRenderer::VT_LABEL_PLACEMENT_TASK_DELAY = 200;
    // A quarter of a zoom level is ~20% more room under the labels - enough to fit a name that did
    // not fit before. The delay is what makes a zoom gesture place once, when it settles.
    const float MapRenderer::LABEL_PLACEMENT_ZOOM_THRESHOLD = 0.25f;
    const int MapRenderer::LABEL_PLACEMENT_ZOOM_DELAY = 250;

    const float MapRenderer::DRAPE_REBAKE_ZOOM_THRESHOLD = 0.25f;

    const int MapRenderer::ELEVATION_REFRESH_DELAY = 500;

    // 2.5 s, the value the android-dev demo settled on while it did this wait itself
    // (DemoMap.TERRAIN_ANIM_TILE_TIMEOUT_MS). Late 3D beats a map pinned flat by one tile that never loads.
    const float MapRenderer::TERRAIN_SWITCH_WARM_TIMEOUT = 2.5f;

    const std::string MapRenderer::BLEND_VERTEX_SHADER = R"GLSL(
        #version 100
        attribute vec2 a_coord;
        uniform mat4 u_mvpMat;
        void main() {
            gl_Position = u_mvpMat * vec4(a_coord, 0.0, 1.0);
        }
    )GLSL";

    const std::string MapRenderer::POST_PROCESS_VERTEX_SHADER = R"GLSL(
        #version 100
        attribute vec2 a_coord;
        void main() {
            gl_Position = vec4(a_coord, 0.0, 1.0);
        }
    )GLSL";

    const std::string MapRenderer::BLEND_FRAGMENT_SHADER = R"GLSL(
        #version 100
        precision mediump float;
        uniform sampler2D u_tex;
        uniform lowp vec4 u_color;
        uniform mediump vec2 u_invScreenSize;
        void main() {
            vec4 texColor = texture2D(u_tex, gl_FragCoord.xy * u_invScreenSize);
            gl_FragColor = texColor * u_color;
        }
    )GLSL";
}

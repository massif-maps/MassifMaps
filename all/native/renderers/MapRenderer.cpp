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
#include "projections/PlanarProjectionSurface.h"
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
#include "renderers/utils/ElevationTextureCache.h"
#include "renderers/utils/GLContext.h"
#include "renderers/utils/GLResourceManager.h"
#include "renderers/utils/FrameBuffer.h"
#include "renderers/PostProcessEffect.h"
#include "renderers/TerrainRenderer.h"
#include "renderers/utils/TerrainDrapeCache.h"
#include "terrain/DrapeStandIn.h"
#include "renderers/utils/TerrainShadowMap.h"
#include "renderers/utils/ScreenMaskBuffer.h"

#include <chrono>
#include <set>
#include "core/MapTile.h"
#include "terrain/AutoFlatten.h"
#include "terrain/CameraClearance.h"
#include "terrain/DrapeStackCuts.h"
#include "terrain/DrapeTuning.h"
#include "terrain/ShadowCasterRing.h"
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
#include <cmath>
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
        // Diagnostic dump of the vt label/tile churn counters (vt/RenderStats.h,
        // MASSIF_VT_RENDER_STATS). All but 'live' are per-interval deltas. Process-wide, so a line
        // covers every map view; the lock keeps two GL threads from tearing the previous values.
        constexpr int RENDER_STATS_INTERVAL = 1000; // ms

        void logRenderStats() {
            using vt::RenderStats;

            static std::mutex statsMutex;
            std::unique_lock<std::mutex> statsLock(statsMutex, std::try_to_lock);
            if (!statsLock.owns_lock()) {
                return; // another renderer is printing this interval
            }

            static const int COUNT = 17;
            static std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
            static long long lastValues[COUNT] = { 0 };

            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            if (now - lastTime < std::chrono::milliseconds(RENDER_STATS_INTERVAL)) {
                return;
            }
            double intervalMs = std::chrono::duration<double, std::milli>(now - lastTime).count();
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

            static long long lastConsidered = 0, lastDistanceCut = 0, lastMaxDistanceCut = 0;
            static long long lastCullPhase[3] = { 0 };
            static long long lastCullFate[6] = { 0 };
            static long long lastLabelMapSkips = 0;
            long long labelMapSkips = RenderStats::labelMapSkips.load();
            long long deltaLabelMapSkips = labelMapSkips - lastLabelMapSkips;
            lastLabelMapSkips = labelMapSkips;

            // One snapshot, printed and stored as 'last' alike: the placement worker keeps counting,
            // and loading twice lost every increment in between.
            const long long considered = RenderStats::cullerConsidered.load();
            const long long distanceCut = RenderStats::cullerDistanceCut.load();
            const long long maxDistanceCut = RenderStats::cullerMaxDistanceCut.load();
            const long long cullPhase[3] = {
                RenderStats::cullerCollectNs.load(), RenderStats::cullerSortNs.load(), RenderStats::cullerInsertNs.load()
            };
            const long long cullFate[6] = {
                RenderStats::cullerInvalid.load(), RenderStats::cullerSorted.load(), RenderStats::cullerVisible.load(),
                RenderStats::cullerNotFacing.load(), RenderStats::cullerCollided.load(), RenderStats::cullerOccluded.load()
            };
            Log::Infof("RenderStats: over %.0f ms | cullUpd=%lld tileRecalc=%lld tileSkip=%lld tileSets=%lld labelMaps=%lld labelMapSkips=%lld | surfBuilt=%lld surfInval=%lld | labelsAlloc=%lld reused=%lld live=%lld elevReanchor=%lld | placeUpd=%lld reNull=%lld reHidden=%lld reVisible=%lld search=%lld | snap=%lld snapMoved=%lld | cullPasses=%lld visFlips=%lld cullMs=%.2f | considered=%lld distCut=%lld styleMaxDistCut=%lld | collectMs=%.1f sortMs=%.1f insertMs=%.1f | invalid=%lld sorted=%lld visible=%lld notFacing=%lld collided=%lld occluded=%lld",
                       intervalMs,
                       deltas[13], deltas[14], deltas[15], deltas[0], deltas[11], deltaLabelMapSkips,
                       deltas[1], deltas[2],
                       deltas[3], deltas[12], RenderStats::labelsLive.load(), deltas[4],
                       deltas[5], deltas[6], deltas[7], deltas[8], deltas[16],
                       deltas[9], deltas[10], deltaPasses, deltaFlips, deltaCullerNs / 1.0e6,
                       considered - lastConsidered,
                       distanceCut - lastDistanceCut,
                       maxDistanceCut - lastMaxDistanceCut,
                       (cullPhase[0] - lastCullPhase[0]) / 1.0e6,
                       (cullPhase[1] - lastCullPhase[1]) / 1.0e6,
                       (cullPhase[2] - lastCullPhase[2]) / 1.0e6,
                       cullFate[0] - lastCullFate[0],
                       cullFate[1] - lastCullFate[1],
                       cullFate[2] - lastCullFate[2],
                       cullFate[3] - lastCullFate[3],
                       cullFate[4] - lastCullFate[4],
                       cullFate[5] - lastCullFate[5]);
            std::copy(cullFate, cullFate + 6, lastCullFate);
            std::copy(cullPhase, cullPhase + 3, lastCullPhase);
            lastConsidered = considered;
            lastDistanceCut = distanceCut;
            lastMaxDistanceCut = maxDistanceCut;

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

            // TerrainRenderer's meshes, built inline on the render thread: 'buildMs' against the frame
            // time says whether a panorama rotating past its mesh cache builds rather than draws.
            {
                static long long lastMeshBuilds = 0, lastMeshBuildUs = 0, lastMeshVerts = 0, lastMeshEvictions = 0, lastMeshHits = 0;
                long long meshBuilds = RenderStats::terrainMeshBuilds.load();
                long long meshBuildUs = RenderStats::terrainMeshBuildUs.load();
                long long meshVerts = RenderStats::terrainMeshVerts.load();
                long long meshEvictions = RenderStats::terrainMeshEvictions.load();
                long long meshHits = RenderStats::terrainMeshCacheHits.load();
                Log::Infof("RenderStats: terrainMesh builds=%lld buildMs=%.1f verts=%lld | evictions=%lld cacheHits=%lld (per interval)",
                           meshBuilds - lastMeshBuilds, (meshBuildUs - lastMeshBuildUs) / 1000.0,
                           meshVerts - lastMeshVerts, meshEvictions - lastMeshEvictions, meshHits - lastMeshHits);
                lastMeshBuilds = meshBuilds; lastMeshBuildUs = meshBuildUs; lastMeshVerts = meshVerts;
                lastMeshEvictions = meshEvictions; lastMeshHits = meshHits;

                static long long lastAttribBakes = 0, lastAttribUs = 0;
                long long attribBakes = RenderStats::terrainAttribBakes.load();
                long long attribUs = RenderStats::terrainAttribUs.load();
                static long long lastRefines = 0, lastRefineUs = 0;
                long long refines = RenderStats::terrainAttribRefines.load();
                long long refineUs = RenderStats::terrainAttribRefineUs.load();
                Log::Infof("RenderStats: terrainAttribs bakes=%lld ms=%.1f (render thread) | refines=%lld ms=%.1f (worker) (per interval)",
                           attribBakes - lastAttribBakes, (attribUs - lastAttribUs) / 1000.0,
                           refines - lastRefines, (refineUs - lastRefineUs) / 1000.0);
                lastAttribBakes = attribBakes; lastAttribUs = attribUs;
                lastRefines = refines; lastRefineUs = refineUs;

                static long long lastFixedVerts = 0, lastTotalVerts = 0;
                long long fixedVerts = RenderStats::terrainAttribFixedVerts.load();
                long long totalVerts = RenderStats::terrainAttribTotalVerts.load();
                long long fixedDelta = fixedVerts - lastFixedVerts;
                long long totalDelta = totalVerts - lastTotalVerts;
                Log::Infof("RenderStats: terrainAttribs fixedScale %lld/%lld verts (%.1f%% resolved a NON-ZERO DEM gradient; the rest are flat or kept the mesh one) (per interval)",
                           fixedDelta, totalDelta, totalDelta > 0 ? 100.0 * fixedDelta / totalDelta : 0.0);
                lastFixedVerts = fixedVerts; lastTotalVerts = totalVerts;

                static long long lastZeroGrid = 0, lastZeroSkirt = 0, lastSkirtOOR = 0;
                long long zeroGrid = RenderStats::terrainAttribZeroGrid.load();
                long long zeroSkirt = RenderStats::terrainAttribZeroSkirt.load();
                long long skirtOOR = RenderStats::terrainAttribSkirtOutOfRange.load();
                Log::Infof("RenderStats: terrainAttribs ZERO normals AS UPLOADED grid=%lld skirt=%lld, skirt source out of range=%lld (per interval)",
                           zeroGrid - lastZeroGrid, zeroSkirt - lastZeroSkirt, skirtOOR - lastSkirtOOR);
                lastZeroGrid = zeroGrid; lastZeroSkirt = zeroSkirt; lastSkirtOOR = skirtOOR;

                static long long lastCarry[4] = { 0 };
                const long long carry[4] = {
                    RenderStats::terrainAttribCarryHit.load(), RenderStats::terrainAttribCarryMissNew.load(),
                    RenderStats::terrainAttribCarryMissUnrefined.load(), RenderStats::terrainAttribCarryMissGrid.load()
                };
                Log::Infof("RenderStats: terrainAttribs refined-normal carry hit=%lld | miss: new tile=%lld, sibling unrefined=%lld, GRID SIZE CHANGED=%lld (per interval)",
                           carry[0] - lastCarry[0], carry[1] - lastCarry[1], carry[2] - lastCarry[2], carry[3] - lastCarry[3]);
                for (int i = 0; i < 4; i++) { lastCarry[i] = carry[i]; }

                static long long lastStretch[4] = { 0 };
                const long long stretch[4] = {
                    RenderStats::terrainAttribStretch1.load(), RenderStats::terrainAttribStretch2.load(),
                    RenderStats::terrainAttribStretch4.load(), RenderStats::terrainAttribStretchBig.load()
                };
                Log::Infof("RenderStats: terrainAttribs DEM coarser than the asked step (tiles) 1x=%lld 2x=%lld 4x=%lld 8x+=%lld (DATA detail only - every tile now SAMPLES at normalSampleDistance) (per interval)",
                           stretch[0] - lastStretch[0], stretch[1] - lastStretch[1], stretch[2] - lastStretch[2], stretch[3] - lastStretch[3]);
                for (int i = 0; i < 4; i++) { lastStretch[i] = stretch[i]; }

                static long long lastTexelRetry = 0;
                Log::Infof("RenderStats: terrainAttribs provisional=%lld final=%lld rebakes=%lld (a provisional bake read a DEM coarser than the source has; cumulative)",
                           RenderStats::terrainAttribProvisional.load(), RenderStats::terrainAttribFinal.load(),
                           RenderStats::terrainAttribRebakes.load());
                Log::Infof("RenderStats: terrainDemTexture hits=%lld misses=%lld (tiles measuring their normal from the GPU elevation texture; cumulative)",
                           RenderStats::terrainDemTextureHits.load(), RenderStats::terrainDemTextureMisses.load());
                long long texelRetry = RenderStats::terrainAttribTexelRetry.load();
                Log::Infof("RenderStats: terrainAttribs texel retries=%lld (vertices exactly flat at the asked step) (per interval)",
                           texelRetry - lastTexelRetry);
                lastTexelRetry = texelRetry;

                static long long lastStale[4] = { 0 };
                const long long stale[4] = {
                    RenderStats::terrainAttribStaleFresh.load(), RenderStats::terrainAttribStale1.load(),
                    RenderStats::terrainAttribStale2.load(), RenderStats::terrainAttribStale3.load()
                };
                Log::Infof("RenderStats: terrainAttribs STALE normals (tiles) fresh=%lld, 1 level=%lld, 2 levels=%lld, 3+=%lld (baked from a coarser DEM than the tile now has) (per interval)",
                           stale[0] - lastStale[0], stale[1] - lastStale[1], stale[2] - lastStale[2], stale[3] - lastStale[3]);
                for (int i = 0; i < 4; i++) { lastStale[i] = stale[i]; }

                static long long lastGrid[5] = { 0 };
                const long long grid[5] = {
                    RenderStats::terrainMeshGrid1.load(), RenderStats::terrainMeshGrid4.load(),
                    RenderStats::terrainMeshGrid16.load(), RenderStats::terrainMeshGrid48.load(),
                    RenderStats::terrainMeshGridFull.load()
                };
                Log::Infof("RenderStats: terrainMesh density (tiles/cut-walk) 1=%lld 4=%lld 16=%lld 48=%lld full=%lld (per interval)",
                           grid[0] - lastGrid[0], grid[1] - lastGrid[1], grid[2] - lastGrid[2],
                           grid[3] - lastGrid[3], grid[4] - lastGrid[4]);
                for (int i = 0; i < 5; i++) { lastGrid[i] = grid[i]; }
            }

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
            static long long lastLabelPass[3] = { 0 };
            const long long labelPass[3] = {
                RenderStats::labelPassSortNs.load(), RenderStats::labelPassPatternNs.load(),
                RenderStats::labelPassStyleNs.load()
            };
            Log::Infof("RenderStats: labelPass sortMs=%.1f patternMs=%.1f styleMs=%.1f (per interval)",
                       (labelPass[0] - lastLabelPass[0]) / 1.0e6, (labelPass[1] - lastLabelPass[1]) / 1.0e6,
                       (labelPass[2] - lastLabelPass[2]) / 1.0e6);
            for (int i = 0; i < 3; i++) { lastLabelPass[i] = labelPass[i]; }
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

            static long long lastSky[3] = { 0 };
            const long long sky[3] = {
                RenderStats::frameClearNs.load(), RenderStats::skyDrawNs.load(),
                RenderStats::backgroundDrawNs.load()
            };
            Log::Infof("RenderStats: sky clearMs=%.1f skyMs=%.1f backgroundMs=%.1f (per interval)",
                       (sky[0] - lastSky[0]) / 1.0e6, (sky[1] - lastSky[1]) / 1.0e6,
                       (sky[2] - lastSky[2]) / 1.0e6);
            for (int i = 0; i < 3; i++) { lastSky[i] = sky[i]; }

            static long long lastLayerHold = 0;
            long long layerHold = RenderStats::layerRefreshHoldNs.load();
            Log::Infof("RenderStats: layerRefreshHoldMs=%.1f (per interval)", (layerHold - lastLayerHold) / 1.0e6);
            lastLayerHold = layerHold;

            // The 'layers' section, split. state+light are per-frame setup that draws nothing.
            static long long lastPass2D[7] = { 0 };
            const long long pass2D[7] = {
                RenderStats::pass2DStateNs.load(), RenderStats::pass2DLightNs.load(),
                RenderStats::pass2DPrepareNs.load(), RenderStats::pass2DGeometryNs.load(),
                RenderStats::pass2DLabels2DNs.load(), RenderStats::pass2DExtrusionNs.load(),
                RenderStats::pass2DLabels3DNs.load()
            };
            Log::Infof("RenderStats: pass2D stateMs=%.1f lightMs=%.1f prepareMs=%.1f geometryMs=%.1f labels2DMs=%.1f extrusionMs=%.1f labels3DMs=%.1f (per interval)",
                       (pass2D[0] - lastPass2D[0]) / 1.0e6, (pass2D[1] - lastPass2D[1]) / 1.0e6,
                       (pass2D[2] - lastPass2D[2]) / 1.0e6, (pass2D[3] - lastPass2D[3]) / 1.0e6,
                       (pass2D[4] - lastPass2D[4]) / 1.0e6, (pass2D[5] - lastPass2D[5]) / 1.0e6,
                       (pass2D[6] - lastPass2D[6]) / 1.0e6);
            for (int i = 0; i < 7; i++) { lastPass2D[i] = pass2D[i]; }

            static long long lastPass3D[3] = { 0 };
            const long long pass3D[3] = {
                RenderStats::pass3DLabels2DNs.load(), RenderStats::pass3DGeometryNs.load(),
                RenderStats::pass3DLabels3DNs.load()
            };
            Log::Infof("RenderStats: pass3D labels2DMs=%.1f geometryMs=%.1f labels3DMs=%.1f (per interval)",
                       (pass3D[0] - lastPass3D[0]) / 1.0e6, (pass3D[1] - lastPass3D[1]) / 1.0e6,
                       (pass3D[2] - lastPass3D[2]) / 1.0e6);
            for (int i = 0; i < 3; i++) { lastPass3D[i] = pass3D[i]; }

            static long long lastExtrusion[9] = { 0 };
            const long long extrusion[6] = {
                RenderStats::extrusionResolveCalls.load(), RenderStats::extrusionResolveHits.load(),
                RenderStats::extrusionResolveUnresolved.load(), RenderStats::extrusionResolveVertices.load(),
                RenderStats::extrusionElevQueries.load(), RenderStats::extrusionResolveNs.load()
            };
            Log::Infof("RenderStats: extrusionBases calls=%lld hits=%lld unresolved=%lld verts=%lld elevQueries=%lld ms=%.1f | bumps=%lld pendingTiles=%lld cleared=%lld (per interval)",
                       extrusion[0] - lastExtrusion[0], extrusion[1] - lastExtrusion[1],
                       extrusion[2] - lastExtrusion[2], extrusion[3] - lastExtrusion[3],
                       extrusion[4] - lastExtrusion[4], (extrusion[5] - lastExtrusion[5]) / 1.0e6,
                       RenderStats::extrusionVersionBumps.load() - lastExtrusion[6],
                       RenderStats::extrusionPendingTiles.load() - lastExtrusion[7],
                       RenderStats::extrusionBasesCleared.load() - lastExtrusion[8]);
            for (int i = 0; i < 6; i++) { lastExtrusion[i] = extrusion[i]; }
            lastExtrusion[6] = RenderStats::extrusionVersionBumps.load();
            lastExtrusion[7] = RenderStats::extrusionPendingTiles.load();
            lastExtrusion[8] = RenderStats::extrusionBasesCleared.load();

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
            static long long lastDrapeClass[5] = { 0 };
            const long long drapeClass[5] = {
                RenderStats::drapeQueuedBlank.load(), RenderStats::drapeQueuedRestack.load(),
                RenderStats::drapeQueuedStandIn.load(), RenderStats::drapeQueuedPartial.load(),
                RenderStats::drapeQueuedStale.load()
            };
            Log::Infof("RenderStats: drape queued blank=%lld restack=%lld standIn=%lld partial=%lld stale=%lld (per interval)",
                       drapeClass[0] - lastDrapeClass[0], drapeClass[1] - lastDrapeClass[1],
                       drapeClass[2] - lastDrapeClass[2], drapeClass[3] - lastDrapeClass[3],
                       drapeClass[4] - lastDrapeClass[4]);
            for (int i = 0; i < 5; i++) { lastDrapeClass[i] = drapeClass[i]; }
            static long long lastStaleWhy[3] = { 0 };
            const long long staleWhy[3] = {
                RenderStats::drapeStaleFingerprint.load(), RenderStats::drapeStaleMask.load(),
                RenderStats::drapeGlobalTermChanges.load()
            };
            Log::Infof("RenderStats: drape stale fingerprint=%lld mask=%lld | globalTermChanges=%lld (per interval)",
                       staleWhy[0] - lastStaleWhy[0], staleWhy[1] - lastStaleWhy[1],
                       staleWhy[2] - lastStaleWhy[2]);
            for (int i = 0; i < 3; i++) { lastStaleWhy[i] = staleWhy[i]; }
            static long long lastDrapeEvict[3] = { 0 };
            const long long drapeEvict[3] = {
                RenderStats::drapeEvictColour.load(), RenderStats::drapeEvictMask.load(),
                RenderStats::drapeMaskAcquireFail.load()
            };
            static long long lastBakeType[6] = { 0 };
            const long long bakeType[6] = {
                RenderStats::drapeBakeLineNs.load(), RenderStats::drapeBakeLineDraws.load(),
                RenderStats::drapeBakePolygonNs.load(), RenderStats::drapeBakePolygonDraws.load(),
                RenderStats::drapeBakeOtherNs.load(), RenderStats::drapeBakeOtherDraws.load()
            };
            Log::Infof("RenderStats: drape bake lines=%lldms/%lld polygons=%lldms/%lld other=%lldms/%lld (per interval)",
                       (bakeType[0] - lastBakeType[0]) / 1000000, bakeType[1] - lastBakeType[1],
                       (bakeType[2] - lastBakeType[2]) / 1000000, bakeType[3] - lastBakeType[3],
                       (bakeType[4] - lastBakeType[4]) / 1000000, bakeType[5] - lastBakeType[5]);
            for (int i = 0; i < 6; i++) { lastBakeType[i] = bakeType[i]; }
            Log::Infof("RenderStats: drape evicted colour=%lld mask=%lld | maskAcquireFail=%lld (per interval)",
                       drapeEvict[0] - lastDrapeEvict[0], drapeEvict[1] - lastDrapeEvict[1],
                       drapeEvict[2] - lastDrapeEvict[2]);
            for (int i = 0; i < 3; i++) { lastDrapeEvict[i] = drapeEvict[i]; }
            // The elevation texture pipeline, which is what extra DEM detail multiplies.
            static long long lastDem[6] = { 0 };
            const long long dem[6] = {
                RenderStats::demEncodes.load(), RenderStats::demBorderPatches.load(),
                RenderStats::demEncodeNs.load(), RenderStats::demUploads.load(),
                RenderStats::demUploadNs.load(), RenderStats::demPatchNs.load()
            };
            // encodeMs is worker time summed over the encode threads, so it can exceed the interval;
            // live/resolved are summed over the caches AND the frames, so only zero/non-zero reads.
            static long long lastDemClears = 0, lastDemTexels = 0;
            static long long lastDemTex[2] = { 0 };
            const long long demTex[2] = { RenderStats::demTexturesLive.load(), RenderStats::demTexturesResolved.load() };
            Log::Infof("RenderStats: dem encodes=%lld patches=%lld encodeWorkerMs=%.1f | uploads=%lld uploadMs=%.1f patchMs=%.1f | liveSum=%lld resolvedSum=%lld zoomGap=%lld caches=%lld detailMask=0x%llx detailClears=%lld texelsPerEncode=%lld (per interval)",
                       dem[0] - lastDem[0], dem[1] - lastDem[1], (dem[2] - lastDem[2]) / 1.0e6,
                       dem[3] - lastDem[3], (dem[4] - lastDem[4]) / 1.0e6, (dem[5] - lastDem[5]) / 1.0e6,
                       demTex[0] - lastDemTex[0], demTex[1] - lastDemTex[1], RenderStats::demTileZoomGap.load(),
                       RenderStats::demCachesLive.load(), RenderStats::demDetailMask.load(),
                       RenderStats::demDetailClears.load() - lastDemClears,
                       (dem[0] - lastDem[0]) > 0 ? (RenderStats::demEncodeTexels.load() - lastDemTexels) / (dem[0] - lastDem[0]) : 0LL);
            lastDemClears = RenderStats::demDetailClears.load();
            lastDemTexels = RenderStats::demEncodeTexels.load();
            // The decoded grid cache, not the texture cache: reinserts > 0 means grids in use are evicted
            // and reloaded, each bumping the elevation version, so labels re-anchor and the surface moves.
            static long long lastElevGrid[2] = { 0 };
            const long long elevGrid[2] = { RenderStats::elevGridInserts.load(), RenderStats::elevGridReinserts.load() };
            Log::Infof("RenderStats: elevGrid inserts=%lld reinserts=%lld | bytes=%lldMB capacity=%lldMB distinctEver=%lld gridKB=%lld managers=%lld (per interval, gauges)",
                       elevGrid[0] - lastElevGrid[0], elevGrid[1] - lastElevGrid[1],
                       RenderStats::elevGridBytes.load() >> 20, RenderStats::elevGridCapacity.load() >> 20,
                       RenderStats::elevGridDistinctEver.load(), RenderStats::elevGridSizeKB.load(),
                       RenderStats::elevGridManagers.load());
            for (int i = 0; i < 2; i++) { lastElevGrid[i] = elevGrid[i]; }

            // Where an elevation lookup lands, over the session. An alias answers with an ancestor's grid
            // and is never asked again, so its tiles keep normals from a too-coarse DEM and shade lighter.
            Log::Infof("RenderStats: elevResolve exact=%lld aliasHits=%lld walkHits=%lld | aliasPuts=%lld (cumulative)",
                       RenderStats::elevExactHits.load(), RenderStats::elevAncestorAliasHits.load(),
                       RenderStats::elevAncestorWalkHits.load(), RenderStats::elevAncestorAliasPuts.load());
            for (int i = 0; i < 6; i++) { lastDem[i] = dem[i]; }
            for (int i = 0; i < 2; i++) { lastDemTex[i] = demTex[i]; }
            // One encode, split. Only meaningful divided by the encodes in the same interval.
            static long long lastDemSplit[3] = { 0 };
            const long long demSplit[3] = {
                RenderStats::demEncodeTextureNs.load(), RenderStats::demEncodeBitmapNs.load(),
                RenderStats::demEncodeNodeNs.load()
            };
            static long long lastNodeBox[2] = { 0 };
            const long long nodeBox[2] = { RenderStats::demNodeEdgeCalls.load(), RenderStats::demNodeBoxTexels.load() };
            static long long lastEncodeCpu = 0;
            long long encodeCpu = RenderStats::demEncodeCpuNs.load();
            Log::Infof("RenderStats: demEncode cpuMs=%.1f (against encodeWorkerMs, per interval)",
                       (encodeCpu - lastEncodeCpu) / 1.0e6);
            lastEncodeCpu = encodeCpu;
            Log::Infof("RenderStats: demNode edgeCalls=%lld boxTexelsPerCall=%lld (per interval)",
                       nodeBox[0] - lastNodeBox[0],
                       (nodeBox[0] - lastNodeBox[0]) > 0 ? (nodeBox[1] - lastNodeBox[1]) / (nodeBox[0] - lastNodeBox[0]) : 0LL);
            for (int i = 0; i < 2; i++) { lastNodeBox[i] = nodeBox[i]; }
            // Which grid answered each texel: only the coarse bucket pays a bilinear sampleHeight, and
            // only it is what latticeRuns/latticeSum would collapse.
            static long long lastNodeSource[3] = { 0, 0, 0 };
            const long long nodeSource[3] = { RenderStats::demNodeTexelsOwn.load(), RenderStats::demNodeTexelsSameLevel.load(), RenderStats::demNodeTexelsCoarse.load() };
            long long nodeSourceTotal = 0;
            for (int i = 0; i < 3; i++) { nodeSourceTotal += nodeSource[i] - lastNodeSource[i]; }
            Log::Infof("RenderStats: demNode texels own=%lld sameLevel=%lld coarse=%lld (%.1f%% coarse, per interval)",
                       nodeSource[0] - lastNodeSource[0], nodeSource[1] - lastNodeSource[1], nodeSource[2] - lastNodeSource[2],
                       nodeSourceTotal > 0 ? 100.0 * (nodeSource[2] - lastNodeSource[2]) / nodeSourceTotal : 0.0);
            for (int i = 0; i < 3; i++) { lastNodeSource[i] = nodeSource[i]; }
            Log::Infof("RenderStats: demEncode textureMs=%.1f bitmapMs=%.1f nodeMs=%.1f (per interval)",
                       (demSplit[0] - lastDemSplit[0]) / 1.0e6, (demSplit[1] - lastDemSplit[1]) / 1.0e6,
                       (demSplit[2] - lastDemSplit[2]) / 1.0e6);
            for (int i = 0; i < 3; i++) { lastDemSplit[i] = demSplit[i]; }

            static long long lastTileLockWait = 0;
            long long tileLockWait = RenderStats::tileRendererLockNs.load();
            Log::Infof("RenderStats: endFrame ms=%.1f swept=%lld labelLockWaitMs=%.1f tileLockWaitMs=%.1f (per interval)",
                       (endFrameNs - lastEndFrame) / 1.0e6, swept - lastSwept,
                       (mutexWait - lastMutexWait) / 1.0e6,
                       (tileLockWait - lastTileLockWait) / 1.0e6);
            lastMutexWait = mutexWait; lastTileLockWait = tileLockWait;
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

    void MapRenderer::resetCameraPlaced() {
        _cameraPlaced = false;
    }

    std::unique_lock<std::recursive_mutex> MapRenderer::holdView() const {
        return std::unique_lock<std::recursive_mutex>(_mutex);
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
        publishViewStateSnapshot(viewState);
        return viewState;
    }

    ViewState MapRenderer::getViewStateSnapshot() const {
        {
            std::lock_guard<std::mutex> lock(_viewStateSnapshotMutex);
            if (_viewStateSnapshot) {
                return *_viewStateSnapshot;
            }
        }
        return getViewState(); // nothing published yet: the first reader pays for one
    }

    void MapRenderer::publishViewStateSnapshot(const ViewState& viewState) const {
        auto snapshot = std::make_shared<const ViewState>(viewState);
        std::lock_guard<std::mutex> lock(_viewStateSnapshotMutex);
        _viewStateSnapshot = snapshot;
    }

    std::shared_ptr<ProjectionSurface> MapRenderer::getProjectionSurface() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::shared_ptr<ProjectionSurface> projectionSurface = _viewState.getProjectionSurface();
        if (!projectionSurface) {
            projectionSurface = _options->getProjectionSurface();
        }
        return projectionSurface;
    }
        
    // Call-site tally for requestRedraw; locked, as requests come from every thread.
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
            // Double-buffered RENDERMODE_WHEN_DIRTY: a lone frame lands in the back buffer, so one more is owed.
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

    std::shared_ptr<ElevationTextureCache> MapRenderer::getElevationTextureCache(const std::shared_ptr<ElevationManager>& elevationManager) {
        if (!elevationManager) {
            return std::shared_ptr<ElevationTextureCache>();
        }
        if (_elevationTextureCache && _elevationTextureCacheManager.lock() != elevationManager) {
            _elevationTextureCache.reset(); // its textures and its encode thread belong to the old manager
        }
        if (!_elevationTextureCache) {
            _elevationTextureCache = std::make_shared<ElevationTextureCache>(elevationManager, getGLResourceManager());
            _elevationTextureCacheManager = elevationManager;
            // An encoded texture is uploaded in beginFrame, so without this a still map never asks for
            // the frame that would apply it: the ground stays flat under labels already at height.
            std::weak_ptr<MapRenderer> mapRendererWeak = shared_from_this();
            _elevationTextureCache->setTextureReadyListener([mapRendererWeak]() {
                if (auto mapRenderer = mapRendererWeak.lock()) {
                    mapRenderer->requestRedraw();
                }
            });
        }
        return _elevationTextureCache;
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

    void MapRenderer::setTouchGestureActive(bool active) {
        if (_touchGestureActive.exchange(active) != active) {
            requestRedraw();
        }
    }
    
    /**
     * Raises only; the frame puts the focus back on the ground. Cached heights only.
     */
    void MapRenderer::constrainCameraToClearance() {
        std::shared_ptr<TerrainOptions> terrainOptions = _options->getTerrainOptions();
        if (!terrainOptions || !terrainOptions->isEnabled()) {
            return;
        }
        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
        std::shared_ptr<ProjectionSurface> projectionSurface = _options->getProjectionSurface();
        if (!elevationManager || !projectionSurface) {
            return;
        }
        MapPos focusMapPos = projectionSurface->calculateMapPos(_viewState.getFocusPos());
        MapPos cameraMapPos = projectionSurface->calculateMapPos(_viewState.getCameraPos());
        double cameraTerrainZ = 0;
        if (!elevationManager->getDisplayHeightCached(cameraMapPos.getX(), cameraMapPos.getY(), cameraTerrainZ)) {
            // Nothing fetches the ground under the camera at low tilt; the focus ground stands in.
            if (!elevationManager->getDisplayHeightCached(focusMapPos.getX(), focusMapPos.getY(), cameraTerrainZ)) {
                return;
            }
        }
        double orbitHeight = cameraMapPos.getZ() - focusMapPos.getZ();
        double clearanceFloor = terrainOptions->getCameraClearance() * elevationManager->getDisplayScale(cameraMapPos.getY());
        double maxZoomOrbit = _viewState.getOrbitDistance(_options->getZoomRange().getMax()) / _viewState.worldPerInternal();
        // Plus the application's lift, as in the frame's own rule.
        double lift = terrainOptions->getFocusLift() * elevationManager->getDisplayScale(focusMapPos.getY()) + _animationHandler.getFlightLift();
        double shellFocusZ = CameraClearance::shellCameraZ(cameraTerrainZ, maxZoomOrbit, clearanceFloor, terrainOptions->getCameraClearanceFraction()) - orbitHeight + lift;
        if (_options->getFreeRoamMode() == FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON) {
            shellFocusZ += _eyeGroundOffset; // mid-glide the eye is meant to be off the newest answer
        }
        if (shellFocusZ > focusMapPos.getZ()) {
            _viewState.setFocusHeight(shellFocusZ);
        }
    }

    bool MapRenderer::landFocusAlongView(const ElevationManager& elevationManager, double lift) {
        std::shared_ptr<ProjectionSurface> projectionSurface = _options->getProjectionSurface();
        cglib::vec3<double> cameraPos = _viewState.getCameraPos();
        cglib::vec3<double> offset = _viewState.getFocusPos() - cameraPos;
        double distance = cglib::length(offset);
        // The frame's pin rule, read through the surface so a globe lands radially.
        auto aboveGround = [&](double d) {
            MapPos mapPos = projectionSurface->calculateMapPos(cameraPos + offset * (d / distance));
            double groundZ = 0;
            if (!elevationManager.getDisplayHeightCached(mapPos.getX(), mapPos.getY(), groundZ)) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            return mapPos.getZ() - (groundZ + lift);
        };
        double newDistance = 0;
        if (!projectionSurface || !CameraClearance::groundAlongView(aboveGround, distance, distance * 16, newDistance)) {
            return false;
        }
        // A tap lands where it stood: marking the camera changed would apply a pending FocusPointOffset.
        if (std::abs(newDistance - distance) <= distance * 1.0e-9) {
            return false;
        }
        _viewState.setFocusPos(cameraPos + offset * (newDistance / distance));
        _viewState.setZoom(static_cast<float>(_viewState.getZoom() + std::log2(distance / newDistance)));
        _viewState.cameraChanged();
        return true;
    }

    /**
     * A cached read answers from a coarse ancestor until the eye's own tile loads, so standing on each answer moved the
     * whole panorama once per level. A change of level is a glide, not a step.
     */
    double MapRenderer::settleEyeGround(const ElevationManager& elevationManager, const MapPos& cameraMapPos, double groundZ, int groundZoom, float deltaSeconds) {
        // A coarser answer for the same spot is whichever grid the lookup read last, not new ground.
        double metre = elevationManager.getDisplayScale(cameraMapPos.getY());
        bool sameSpot = _eyeGroundZoom >= 0 && std::abs(cameraMapPos.getX() - _eyeGroundX) < metre && std::abs(cameraMapPos.getY() - _eyeGroundY) < metre;
        if (sameSpot && groundZoom < _eyeGroundZoom) {
            groundZ = _eyeGroundTarget;
            groundZoom = _eyeGroundZoom;
        } else {
            _eyeGroundX = cameraMapPos.getX();
            _eyeGroundY = cameraMapPos.getY();
        }
        _eyeGroundTarget = groundZ;
        if (_eyeGroundZoom >= 0 && groundZoom != _eyeGroundZoom) {
            _eyeGroundOffset = _eyeGroundZ - groundZ;
#if MASSIF_FRAME_PROFILER
            Log::Infof("MapRenderer: eye ground z%d -> z%d, glides %.1f m", _eyeGroundZoom, groundZoom, _eyeGroundOffset / std::max(1e-12, metre));
#endif
        }
        _eyeGroundZoom = groundZoom;
        _eyeGroundOffset *= std::exp(-deltaSeconds / EYE_GROUND_SETTLE_TIME);
        if (std::abs(_eyeGroundOffset) < 0.01 * metre) {
            _eyeGroundOffset = 0;
        } else {
            requestRedraw();
        }
        _eyeGroundZ = groundZ + _eyeGroundOffset;
        return _eyeGroundZ;
    }

    void MapRenderer::calculateCameraEvent(CameraPanEvent& cameraEvent, float durationSeconds, bool updateKinetic, MapMoveReason::MapMoveReason reason) {
        if (durationSeconds > 0) {
            if (cameraEvent.isUseDelta()) {
                _animationHandler.setPanDelta(cameraEvent.getPosDelta(), durationSeconds);
            } else {
                _animationHandler.setPanTarget(cameraEvent.getPos(), durationSeconds);
            }
    
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
        
            cameraEvent.calculate(*_options, _viewState);
            _cameraPlaced = true;

            newFocusPos = projectionSurface->calculateMapPos(_viewState.getFocusPos());
            zoom = _viewState.getZoom();
            // After the kinetic read: a vertical lift folded into the 3D fling delta flings the map away.
            constrainCameraToClearance();
          
            // In case of seamless panning horizontal teleport, offset the delta focus pos
            oldFocusPos.setX(oldFocusPos.getX() + _viewState.getHorizontalLayerOffsetDir() * Const::WORLD_SIZE);
        }
    
        // Delayed: the view state is only updated in onDrawFrame.
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
    
            requestRedraw();
            return;
        }

        MapPos focusPos;
        float deltaRotation;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            float oldRotation = _viewState.getRotation();
            
            cameraEvent.calculate(*_options, _viewState);
            _cameraPlaced = true;

            float rotation = _viewState.getRotation();
            deltaRotation = rotation - oldRotation;

            focusPos = getProjectionSurface()->calculateMapPos(_viewState.getFocusPos());
            constrainCameraToClearance(); // after the kinetic read, as in the pan overload
        }
    
        // Delayed: the view state is only updated in onDrawFrame.
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
    
            requestRedraw();
            return;
        }
    
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            
            cameraEvent.calculate(*_options, _viewState);
            _cameraPlaced = true;
            constrainCameraToClearance();
        }
    
        // Delayed: the view state is only updated in onDrawFrame.
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
    
            requestRedraw();
            return;
        }
    
        MapPos focusPos;
        float deltaZoom;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            float oldZoom = _viewState.getZoom();
            
            cameraEvent.calculate(*_options, _viewState);
            _cameraPlaced = true;

            float zoom = _viewState.getZoom();
            deltaZoom = zoom - oldZoom;

            focusPos = getProjectionSurface()->calculateMapPos(_viewState.getFocusPos());
            constrainCameraToClearance(); // after the kinetic read, as in the pan overload
        }
    
        // Delayed: the view state is only updated in onDrawFrame.
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
            
            // Binary search for the zoom that fits all the points.
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
            
            // Reset to the starting state, then animate to the final one.
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

        // One-time GL diagnostics: depth/stencil bits and vertex texture units decide the terrain depth model.
        {
            GLint depthBits = 0, stencilBits = 0, maxVertexTextureUnits = 0;
            glGetIntegerv(GL_DEPTH_BITS, &depthBits);
            glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
            glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxVertexTextureUnits);
            const GLubyte* renderer = glGetString(GL_RENDERER);
            Log::Infof("MapRenderer::onSurfaceCreated: renderer '%s', depth bits %d, stencil bits %d, vertex texture units %d",
                renderer ? reinterpret_cast<const char*>(renderer) : "?", depthBits, stencilBits, maxVertexTextureUnits);
        }

        if (_surfaceCreated) {
            onSurfaceDestroyed();
        }
        _surfaceCreated = true;
        _surfaceChanged = true; // should not be needed, do it in any case

        if (_glResourceManager) {
            _glResourceManager->setGLThreadId(std::thread::id());
        }
        _glResourceManager = std::make_shared<GLResourceManager>();
        _glResourceManager->setGLThreadId(std::this_thread::get_id());

        // The GPU timer queries belong to the context that generated them, and this is a new one.
        FRAME_PROF_GPU_RESET();

        _screenBoundFBOs.clear();
        _screenFrameBuffers.clear();
        _screenBlendShader.reset();

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

        // Windows Phone may call onSurfaceCreated/onSurfaceChanged from different threads.
        _glResourceManager->setGLThreadId(std::this_thread::get_id());

        _glResourceManager->processResources();

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

            viewChanged(false, MapMoveReason::MAP_MOVE_REASON_API);
        }
        
        std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
        float deltaSeconds = 1.0f / 60.0f;
        if (_lastFrameTime) {
            deltaSeconds = std::max(0.0f, std::chrono::duration_cast<std::chrono::duration<float> >(currentTime - *_lastFrameTime).count());
        }
        _lastFrameTime = currentTime;
    
        if (mapRendererListener) {
            mapRendererListener->onBeforeDrawFrame();
        }

        // Off _mutex: option listeners are app code, and could deadlock against any facade getter.
        bool terrainDecodeChanged = updateTerrainFlatten(deltaSeconds);

        ViewState viewState;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            // Positions go through the surface: on a sphere xyz is a 3D point, not x/y plus height.
            std::shared_ptr<ElevationManager> elevationManager;
            std::shared_ptr<TerrainOptions> focusTerrainOptions;
            std::shared_ptr<ProjectionSurface> projectionSurface = _options->getProjectionSurface();
            if (auto terrainOptions = _options->getTerrainOptions()) {
                if (terrainOptions->isEnabled() && projectionSurface) {
                    elevationManager = terrainOptions->getElevationManager();
                    focusTerrainOptions = terrainOptions;
                }
            }
            if (elevationManager) {
                // maplibre's model: the focus sits on the ground, so a zoom is the distance to it at every altitude; while a
                // finger drags or a fling runs it keeps its height (a ridge pan would bob), and is put back when it ends.
                {
                    MapPos focusMapPos = projectionSurface->calculateMapPos(_viewState.getFocusPos());
                    MapPos cameraMapPos = projectionSurface->calculateMapPos(_viewState.getCameraPos());
                    double orbitHeight = cameraMapPos.getZ() - focusMapPos.getZ(); // invariant under the lift
                    // App lift on top, kept out of `follow` so it cannot feed its own input.
                    double lift = focusTerrainOptions->getFocusLift() * elevationManager->getDisplayScale(focusMapPos.getY()) + _animationHandler.getFlightLift();
                    double terrainZ = 0;
                    if (_options->getFreeRoamMode() == FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON) {
                        // The ground under the camera, never the focus's: at a panorama the focus is far off, often uncached.
                        double groundZ = 0;
                        int groundZoom = -1;
                        bool groundKnown = elevationManager->getDisplayHeightCached(cameraMapPos.getX(), cameraMapPos.getY(), groundZ, groundZoom);
                        elevationManager->requestTileGridAt(cameraMapPos.getX(), cameraMapPos.getY(), groundZoom, 2);
                        if (groundKnown) {
                            _viewState.setFocusHeight(settleEyeGround(*elevationManager, cameraMapPos, groundZ, groundZoom, deltaSeconds) - orbitHeight + lift);
                        }
                    } else if (elevationManager->getDisplayHeightCached(focusMapPos.getX(), focusMapPos.getY(), terrainZ)) {
                        // Measured with the focus pinned, so the lift cannot feed back into its input.
                        double cameraTerrainZ = terrainZ;
                        elevationManager->getDisplayHeightCached(cameraMapPos.getX(), cameraMapPos.getY(), cameraTerrainZ);
                        double clearanceFloor = focusTerrainOptions->getCameraClearance() * elevationManager->getDisplayScale(cameraMapPos.getY());
                        double maxZoomOrbit = _viewState.getOrbitDistance(_options->getZoomRange().getMax()) / _viewState.worldPerInternal();
                        double clearanceFraction = focusTerrainOptions->getCameraClearanceFraction();
                        // Never below the shell: raising keeps the user's tilt and zoom.
                        double shellFocusZ = CameraClearance::shellCameraZ(cameraTerrainZ, maxZoomOrbit, clearanceFloor, clearanceFraction) - orbitHeight;
                        _eyeGroundZoom = -1;
                        _eyeGroundOffset = 0;
                        bool gesture = _touchGestureActive.load() || _kineticEventHandler.isPanning() || _kineticEventHandler.isZooming() || _kineticEventHandler.isRotating();
                        if (gesture) {
                            _terrainFocusFrozen = true;
                            _viewState.setFocusHeight(std::max(focusMapPos.getZ(), shellFocusZ + lift));
                        } else {
                            double pinnedZ = std::max(terrainZ, shellFocusZ) + lift;
                            // On the ground, not the shell: the next frame raises a camera under it, and only then.
                            bool landed = _terrainFocusFrozen && landFocusAlongView(*elevationManager, lift);
                            _terrainFocusFrozen = false;
                            if (landed) {
                                requestRedraw();
                            } else if (pinnedZ != focusMapPos.getZ()) {
                                // A finer DEM tile moves the ground under a still camera: without a frame for it, the
                                // picture stays at the old height.
                                _viewState.setFocusHeight(pinnedZ);
                                requestRedraw();
                            }
                        }
                    }
                }
                MapPos cameraMapPos = projectionSurface->calculateMapPos(_viewState.getCameraPos());
                double minZ = 0, maxZ = 0;
                elevationManager->getDisplayHeightRange(cameraMapPos.getY(), minZ, maxZ);
                _viewState.setTerrainHeightRange(static_cast<float>(minZ), static_cast<float>(maxZ));

                // Not clamped here: mutating the camera outside camera events breaks ViewState's zoom invariant.

                // Debounced refresh so vector element draw data picks up new heights.
                unsigned int elevationVersion = elevationManager->getVersion();
                if (elevationVersion != _layersElevationVersion) {
                    if (!_lastElevationRefreshTime || currentTime - *_lastElevationRefreshTime > std::chrono::milliseconds(ELEVATION_REFRESH_DELAY)) {
                        _layersElevationVersion = elevationVersion;
                        _lastElevationRefreshTime = currentTime;
                        for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
                            if (std::dynamic_pointer_cast<VectorLayer>(layer)) {
                                layer->refresh();
                            } else if (std::dynamic_pointer_cast<TileLayer>(layer)) {
                                // The LOD projects at DEM height; nothing else re-culls a still camera.
                                layerChanged(layer, true);
                            }
                        }
                    } else {
                        requestRedraw(); // check again on the next frame
                    }
                }
            } else {
                _viewState.setFocusHeight(_animationHandler.getFlightLift()); // back onto the surface, radially on a globe
                _viewState.setTerrainHeightRange(0.0f, 0.0f);
            }

            _viewState.calculateViewState(*_options);
            viewState = _viewState;
            _viewState.setHorizontalLayerOffsetDir(0);

        }
        publishViewStateSnapshot(viewState);

        if (terrainDecodeChanged) {
            // LOD, overzoom and view distance differ between decode states; the camera has not moved.
            viewChanged(false, MapMoveReason::MAP_MOVE_REASON_API);
        }

        _animationHandler.calculate(viewState, deltaSeconds);
        _kineticEventHandler.calculate(viewState, deltaSeconds);

        std::shared_ptr<PostProcessEffect> postProcessEffect = getPostProcessEffect();
        bool sceneDepth = false;
        if (postProcessEffect) {
            // The terrain depth an effect reads comes from the scene's own depth when it can (see
            // applyPostProcessEffect), so the scene's depth is kept as a texture for it.
            sceneDepth = postProcessEffect->isTerrainDepthRequired() && !postProcessEffect->isTerrainNormalsRequired();
            clearAndBindScreenFBO(_options->getClearColor(), true, false, sceneDepth);
        }
        if (_terrainRenderer) {
            _terrainRenderer->setSurfacePolygonOffset(!sceneDepth);
        }

        // Resolved once before anything draws, so the sky and background fog like the tiles.
        _frameStyleEnvironment = collectStyleEnvironment(viewState);
        _frameFog = resolveFog(_options->getFogOptions(), _frameStyleEnvironment,
                               resolveLighting(_options->getLightOptions(), _frameStyleEnvironment),
                               viewState.calculateCameraDistance(), viewState.getTilt());

        FRAME_PROF_NOW(profFrameStart);
        FRAME_PROF_RESET();
        FRAME_PROF_GPU_BEGIN(SECTION_SKY);
        VT_STAT_CLOCK(skyClock);
        initializeRenderState();
        VT_STAT_SPLIT(frameClearNs, skyClock);
        // The shader sky replaces the legacy sky band when it draws.
        bool skyDrawn = _skyRenderer.onDrawFrame(viewState, _frameFog, resolveSky(_options->getSkyOptions(), _frameStyleEnvironment));
        VT_STAT_SPLIT(skyDrawNs, skyClock);
        // Timed apart from the sky: the first section also absorbs GPU idle time (GpuFrameProfiler).
        FRAME_PROF_GPU_BEGIN(SECTION_BACKGROUND);
        // Measurement switch: tangram's background is only the clear colour (core/src/map.cpp).
        if (isBackgroundEnabled() && !isGroundCovered(viewState, skyDrawn, static_cast<bool>(postProcessEffect))) {
            _backgroundRenderer.onDrawFrame(viewState, _frameFog, !skyDrawn);
        }
        VT_STAT_SPLIT(backgroundDrawNs, skyClock);
        FRAME_PROF_ADD(skyMs, profFrameStart);
        drawLayers(deltaSeconds, viewState, static_cast<bool>(postProcessEffect));
        FRAME_PROF_GPU_END();
        FRAME_PROF_END(profFrameStart);
        if (postProcessEffect) {
            // With opted-out overlay layers, the effect resolves offscreen and is blitted after them.
            bool overlays = !_overlayLayers.empty();
            applyPostProcessEffect(postProcessEffect, viewState, overlays);
            if (overlays) {
                drawOverlayLayers(deltaSeconds, viewState);
                blendAndUnbindScreenFBO(1.0f);
            }
        }

        if (mapRendererListener) {
            mapRendererListener->onAfterDrawFrame();
        }

        handleRendererCaptureCallbacks();
        
        if (_billboardsChanged.exchange(false)) {
            _billboardPlacementWorker->init(BILLBOARD_PLACEMENT_TASK_DELAY);
        }
        
        // Before the idle test, so the map is not announced idle with a frame still owed.
        if (_redrawExtraFrames.load() > 0) {
            _redrawExtraFrames--;
            DirectorPtr<RedrawRequestListener> redrawRequestListener = _redrawRequestListener;
            if (redrawRequestListener) {
                _redrawPending = true;
                redrawRequestListener->onRedrawRequested();
            }
        }

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

#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
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

    bool MapRenderer::isGroundCovered(const ViewState& viewState, bool skyDrawn, bool postProcessing) const {
        // The tiles paint the style background themselves; the plane under them costs a full-screen pass.
        if ((!skyDrawn && viewState.isSkyVisible()) || viewState.getHorizontalLayerOffsetDir() != 0) {
            return false;
        }
        std::vector<std::shared_ptr<Layer> > layers = _layers->getAll();
        if (layers.empty() || (postProcessing && !layers.front()->isPostProcessed())) {
            return false;
        }
        auto tileLayer = std::dynamic_pointer_cast<TileLayer>(layers.front());
        return tileLayer && tileLayer->isVisible() && tileLayer->getOpacity() >= 1.0f && tileLayer->getVisibleZoomRange().inRange(viewState.getZoom()) && tileLayer->coversGround(viewState);
    }

    void MapRenderer::onSurfaceDestroyed() {
        // This method may never be called (e.x Android)
        _surfaceCreated = false;

        // Invalidating the thread ids makes the managers ignore resource releases.
        if (_glResourceManager) {
            _glResourceManager->setGLThreadId(std::thread::id());
            _glResourceManager.reset();
        }

        _screenBoundFBOs.clear();
        _screenFrameBuffers.clear();
        _screenBlendShader.reset();

        // Their handles belong to the dying context.
        _terrainDrapeCache.reset();
        _terrainShadowMap.reset();
        _terrainShadowMaskBuffer.reset();
        _groundAOMaskBuffer.reset();
        _groundAODrapeBuffer.reset();
        _shadowMapValid = false;

        _backgroundRenderer.onSurfaceDestroyed();
        _skyRenderer.onSurfaceDestroyed();
    }
    
    void MapRenderer::finishRendering() {
        glFinish();
    }
    
    void MapRenderer::clearAndBindScreenFBO(const Color& color, bool depth, bool stencil, bool depthTexture) {
        GLint prevBoundFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevBoundFBO);
        GLuint bufferMask = GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0) | (stencil ? GL_STENCIL_BUFFER_BIT : 0);
        // The key carries the depth-texture choice in a bit no GL buffer mask uses; every user of the
        // stored mask tests it with '&', so it passes through them.
        GLuint bufferKey = bufferMask | (depthTexture ? SCREEN_FBO_DEPTH_TEXTURE_BIT : 0);
        _screenBoundFBOs.emplace_back(static_cast<GLuint>(prevBoundFBO), bufferKey);

        std::shared_ptr<FrameBuffer>& frameBuffer = _screenFrameBuffers[bufferKey];
        if (!frameBuffer || !frameBuffer->isValid()) {
            frameBuffer = _glResourceManager->create<FrameBuffer>(_viewState.getWidth(), _viewState.getHeight(), true, depth, stencil, depthTexture);
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
            // The clock restarts only for a different effect, not a parameter change.
            if (_postProcessEffect != postProcessEffect) {
                _postProcessEffect = postProcessEffect;
                _postProcessStartTime = std::chrono::steady_clock::now();
            }
        }
        // Unconditional: re-setting the same effect is how a parameter change asks for a frame.
        requestRedraw();
    }

    void MapRenderer::applyPostProcessEffect(const std::shared_ptr<PostProcessEffect>& effect, const ViewState& viewState, bool keepBound) {
        static const GLfloat screenVertices[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };

        if (_screenBoundFBOs.empty()) {
            Log::Error("MapRenderer::applyPostProcessEffect: No bound FBOs");
            return;
        }

        GLuint terrainDepthTex = 0;
        // The scene's depth, when it was kept as a texture: converting it is one full-screen pass,
        // where drawing the terrain again into the depth texture was the whole mesh a second time -
        // 22% of the frame rate of a panorama on an Adreno 610.
        GLuint sceneDepthTex = 0;
        if (!_screenBoundFBOs.empty() && (_screenBoundFBOs.back().second & SCREEN_FBO_DEPTH_TEXTURE_BIT)) {
            std::shared_ptr<FrameBuffer>& sceneFrameBuffer = _screenFrameBuffers[_screenBoundFBOs.back().second];
            if (sceneFrameBuffer && sceneFrameBuffer->isValid()) {
                sceneDepthTex = sceneFrameBuffer->getDepthTexId();
            }
        }
        if (effect->isTerrainDepthRequired()) {
            std::shared_ptr<TerrainOptions> terrainOptions = _options->getTerrainOptions();
            if (terrainOptions && terrainOptions->isActive()) {
                if (!_terrainRenderer) {
                    _terrainRenderer = std::make_unique<TerrainRenderer>();
                    _terrainRenderer->setTileTransformer(_options->getTileTransformer());
                }
                // Full resolution, or a line effect draws the coarse mesh's triangulation.
                if (std::shared_ptr<ElevationManager> depthElevation = terrainOptions->getElevationManager()) {
                    _terrainRenderer->setElevationTextureCache(getElevationTextureCache(depthElevation));
                }
                bool rendered = false;
                if (sceneDepthTex != 0 && !effect->isTerrainNormalsRequired()) {
                    rendered = _terrainRenderer->renderDepthTextureFromScene(viewState, terrainOptions, _glResourceManager, sceneDepthTex);
                }
                if (!rendered) {
                    rendered = _terrainRenderer->renderDepthTexture(viewState, terrainOptions, _glResourceManager, 0, effect->isTerrainNormalsRequired());
                }
                if (rendered) {
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
            // Resolve into the second color texture, keeping the depth for opted-out layers.
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
        // A post-process effect's outermost unwind sends the secondary texture, then re-attaches the primary.
        GLuint colorTexId = frameBuffer->getColorTexId();
        // The finished frame REPLACES the screen: blended, a translucent one (AR) piled every
        // earlier frame up under it, and nothing else clears the screen on this path.
        bool finishedFrame = false;
        if (_postProcessSecondaryActive && _screenBoundFBOs.empty()) {
            colorTexId = frameBuffer->getAttachedColorTexId();
            frameBuffer->attachSecondaryColorTex(false);
            _postProcessSecondaryActive = false;
            finishedFrame = true;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, prevBoundFBO);

        if (!_screenBlendShader || !_screenBlendShader->isValid()) {
            _screenBlendShader = _glResourceManager->create<Shader>("blend", BLEND_VERTEX_SHADER, BLEND_FRAGMENT_SHADER);
        }
        
        glUseProgram(_screenBlendShader->getProgId());

        // Never depth-tested: the screen framebuffer's depth is never cleared, so later blits would fail.
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
        
        if (finishedFrame) {
            glDisable(GL_BLEND);
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (finishedFrame) {
            glEnable(GL_BLEND);
        }
        
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
        // dst *= mask; white elsewhere, so no depth test. No culling: the strip's triangles wind opposite ways.
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
        // Missing or partial DEM is not flat: flattening would stop the decode for good. Unknown = not yet.
        if (!(maxZ > minZ) || !_autoFlattenSeenTerrain || _autoFlattenDataQuiet < TERRAIN_SWITCH_WARM_TIMEOUT) {
            return std::numeric_limits<double>::infinity();
        }
        double halfWidth = _viewState.getHalfWidth(), halfHeight = _viewState.getHalfHeight();
        // Height range is internal units, camera distance world units (18-globe.md).
        return AutoFlatten::parallax(std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight), (maxZ - minZ) * _viewState.worldPerInternal(), _viewState.calculateCameraDistance());
    }

    void MapRenderer::reportFlattenSwitchTiming(const FlattenSwitch::State& state, const FlattenSwitch::Input& input, int tilesOwed, float deltaSeconds) {
        FlattenSwitchTimeline::Input timing;
        timing.phase = state.phase;
        timing.deltaSeconds = deltaSeconds;
        timing.tilesOwed = tilesOwed;
        timing.warmTimedOut = input.warmTimeout > 0 && state.warmSeconds >= input.warmTimeout;
        // Consumed: a flat frame never reaches the drape, and a stale true would hold the report open.
        timing.bakes = _drapeBakesDone;
        timing.bakesQueued = _drapeBakesPending;
        _drapeBakesDone = 0;
        _drapeBakesPending = false;
        // Not isUpdateInProgress(): a composite takes _sourceMutex for it, and this runs on the
        // render thread under _mutex - the deadlock snapshotChildTileLayers exists to avoid.
        FlattenSwitchTimeline::Report report;
        if (!_flattenSwitchTimeline.step(timing, report)) {
            return;
        }
#if MASSIF_VT_RENDER_STATS
        // One line per switch: which of the three halves the user was actually waiting on.
        Log::Infof("MapRenderer: %s switch took %.0f ms - warm %.0f ms (%d frames, %d tiles owed%s), ramp %.0f ms (%d frames, %.1f fps), settle %.0f ms (%d frames, %d bakes)",
            report.rising ? "2D->3D" : "3D->2D", report.totalSeconds() * 1000.0f,
            report.warmSeconds * 1000.0f, report.warmFrames, report.tilesOwed, report.timedOut ? ", TIMED OUT" : "",
            report.rampSeconds * 1000.0f, report.rampFrames,
            report.rampSeconds > 0 ? report.rampFrames / report.rampSeconds : 0.0f,
            report.settleSeconds * 1000.0f, report.settleFrames, report.bakes);
#endif
    }

    bool MapRenderer::updateTerrainFlatten(float deltaSeconds) {
        std::shared_ptr<TerrainOptions> terrainOptions = _options->getTerrainOptions();
        if (!terrainOptions || !terrainOptions->isEnabled()) {
            return false;
        }

        // Terrain reached at least once: only then may the rule flatten. See the member.
        _autoFlattenSeenTerrain = _autoFlattenSeenTerrain || _flattenSwitchState.phase == FlattenSwitch::Phase::TERRAIN;

        // How long no DEM tile has landed; calculateTerrainParallax will not decide before it.
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
            // Read together under _mutex; decided without it, as setFlattened reaches application code.
            double parallax = 0;
            float tilt = 0;
            bool cameraPlaced = false;
            {
                std::lock_guard<std::recursive_mutex> lock(_mutex);
                // A height-range lookup, only when part of the rule.
                parallax = parallaxThreshold > 0 ? calculateTerrainParallax(terrainOptions) : 0;
                tilt = _viewState.getTilt();
                cameraPlaced = _cameraPlaced;
            }
            bool flatten = AutoFlatten::shouldFlatten(parallax, parallaxThreshold, tilt, tiltThreshold, terrainOptions->isFlattened());
            // Only on a CHANGE of the rule's own answer - see AutoFlatten::Trigger.
            if (_autoFlattenTrigger.changed(flatten, cameraPlaced)) {
                Log::Infof("MapRenderer: auto-flatten %s (parallax %.1f px vs %.1f, tilt %.1f vs %.1f, data quiet %.1f s, seen terrain %d)",
                    flatten ? "ON" : "off", parallax, parallaxThreshold, tilt, tiltThreshold, _autoFlattenDataQuiet, _autoFlattenSeenTerrain ? 1 : 0);
                terrainOptions->setFlattened(flatten);
                manual = terrainOptions->isManualFlatten(); // setFlattened hands the ratio back
            }
        } else {
            // Rule turned off while its last answer was on: release what it set (never an explicit setFlattened),
            // or the map stays flat for good.
            if (_autoFlattenTrigger.last == 1 && !manual && terrainOptions->isFlattened()) {
                Log::Info("MapRenderer: auto-flatten disabled while ON - releasing the flat state it set");
                terrainOptions->setFlattened(false);
            }
            _autoFlattenTrigger = AutoFlatten::Trigger(); // re-arm: the next answer is an edge again
        }

        // Seeded from what was asked, so a map starting in 2D neither animates nor decodes 3D.
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
        int tilesOwed = 0;
        if (FlattenSwitch::isWaitingForTiles(_flattenSwitchState, input)) {
            input.tilesReady = true;
            for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
                if (auto tileLayer = std::dynamic_pointer_cast<TileLayer>(layer)) {
                    input.tilesReady = tileLayer->isTerrainDecodeSettled() && input.tilesReady;
                    int owed = tileLayer->getTerrainDecodePendingCount();
                    tilesOwed = owed < 0 || tilesOwed < 0 ? -1 : tilesOwed + owed;
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
        reportFlattenSwitchTiming(next, input, tilesOwed, deltaSeconds);
        terrainOptions->setSwitching(FlattenSwitch::isWaitingForTiles(next, input));
        if (next.phase == FlattenSwitch::Phase::RAMPING) {
            // The ramp's first frame has no delta yet, and nothing else asks for the next one.
            requestRedraw();
        }
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
        return decodeChanged;
    }

    void MapRenderer::vtLabelsChanged(const std::shared_ptr<Layer>& layer, bool delay) {
        _vtLabelPlacementWorker->init(layer, delay ? VT_LABEL_PLACEMENT_TASK_DELAY : 0);
    }
    
    void MapRenderer::layerChanged(const std::shared_ptr<Layer>& layer, bool delay) {
        // Before the surface exists, onSurfaceChanged starts the cull worker instead.
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

        // Zooming changes screen-space envelopes without a tile set change; postponed, so a gesture places once.
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
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    
        // Premultiplied alpha.
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    
        glDisable(GL_DITHER);
    
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
    
    // Minimum refresh rate: streamed-in elevation changes neither light box nor caster list.

    // Screen divisor for the terrain shadow mask; its edges are penumbrae, so a quarter is invisible.
    static const int SHADOW_MASK_DIVISOR = 4;
    // Half for contact shadows: only metres wide, a quarter would average their gradient away.
    static const int GROUND_AO_MASK_DIVISOR = 2;
    // Caster ring zoom: one tile (~28 km at 45 degrees) spans a massif shadowing the view.
    static const int SHADOW_RELIEF_ZOOM = 10;
    // Ring subdivision is 4^(maxCoverZoom - ringZoom); this cap turns an OOM into a frame cost.
    static const std::size_t MAX_SHADOW_CASTER_TILES = 2048;
    static const int SHADOW_MAP_MAX_AGE = 30;
    // Frames between two refreshes driven by newly arrived tile content.
    static const int SHADOW_MAP_CONTENT_INTERVAL = 4;
    // Extrusion growth, in tile heights, between refreshes: about a dozen per fade, not one per frame.
    static const float SHADOW_MAP_FADE_STEP = 0.08f;

    // Cumulative caster-pass counters, against the frame count: what the shadow map cache saves.
    static int shadowPasses = 0;
    static int shadowCasterDraws = 0;
    static int shadowExtrusionDraws = 0;
    static int shadowCastersNoElevation = 0;
    static double shadowMsSum = 0;

    bool MapRenderer::getShadowSunDir(cglib::vec3<float>& sunDir) const {
        std::lock_guard<std::mutex> lock(_shadowSunMutex);
        sunDir = _shadowSunDir;
        return _shadowSunActive;
    }

    void MapRenderer::applyTerrainShadows(const std::vector<std::shared_ptr<TileLayer> >& tileLayers, const std::vector<vt::TileId>& coverTileIds, const std::shared_ptr<TerrainOptions>& terrainOptions, const ViewState& viewState, int prevFBO, bool contentChanged, bool castShadows, ResolvedLighting& lighting, std::array<double, TerrainShadowMap::MAX_CASCADES>& shadowTexelMeters) {
        // Casters and receivers share one vertex shader and elevation fetch, so they cannot disagree.
        float shadowStrength = 0.0f;
        unsigned int shadowTexture = 0;
        int shadowMapSize = 0, shadowCascades = 1;
        float shadowSoftness = 1.0f;
        
        // mapbox's u_shadow_bias (shadow_renderer.ts): constant, slope scale, slope cap, in normalised
        // light depth; LightOptions' ShadowBias scales it, 1 = theirs.
        cglib::vec3<float> shadowBias(0.0f, 0.0f, 0.0f);
        std::array<float, TerrainShadowMap::MAX_CASCADES> shadowDepthScales = { };
        std::array<cglib::mat4x4<double>, TerrainShadowMap::MAX_CASCADES> lightViewProjs;
        lightViewProjs.fill(cglib::mat4x4<double>::identity());
        // Style over LightOptions, re-read every frame so it may follow the zoom.
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
                    // Keep the azimuth; only the altitude is raised.
                    shadowSunDir(0) *= scale / horizontal;
                    shadowSunDir(1) *= scale / horizontal;
                }
                shadowSunDir(2) = MIN_SHADOW_SUN_SIN;
            }
        }
        // mapbox's constants verbatim; the pair depends on the normal offset.
        {
            // mapbox's shape in metres (our light box spans huge distances); constant halved with normal offset.
            float scale = std::max(0.0f, lighting.shadowBias);
            float constant = (lighting.shadowNormalOffset > 0.0f ? 1.0f : 2.0f);
            shadowBias = cglib::vec3<float>(constant, 0.25f, 4.0f) * scale;
        }
        bool shadowsWanted = false;
        {
            // Dropped during the 2D/3D ramp rather than re-cast every frame.
            bool switching = terrainOptions->getFlattenRatio() > 0.0f;
            shadowsWanted = castShadows && !switching && lighting.terrainLightingEnabled && lighting.shadowStrength > 0.0f && !coverTileIds.empty();
            if (shadowsWanted) {
                if (!_terrainShadowMap) {
                    _terrainShadowMap = std::make_unique<TerrainShadowMap>();
                }
                _terrainShadowMap->setSize(lighting.shadowMapSize, lighting.shadowCascades);
                // At a low sun the box stretches by the height range / tan(altitude): keep the slab tight.
                double minHeight = 0, maxHeight = 0;
                // Per tile too, so a cascade fits its box to its own piece's relief.
                std::vector<std::pair<double, double> > tileHeights;
                if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                    bool first = true;
                    tileHeights.reserve(coverTileIds.size());
                    // Only tiles with real elevation: the inexact getter falls back to the session's highest ground.
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
                // Casters reach beyond the visible tiles: off-screen mountains shadow the view.
                std::vector<vt::TileId> casterTileIds = coverTileIds;
                int casterMargin = lighting.shadowCasterMargin;
                if (casterMargin > 0) {
                    // Must stay a partition: overlapping casters at different DEM levels self-shadow the receiver.
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
                    // Ring reach = shadow throw (relief / tan(altitude), 15-degree floor), at the coarsest zoom spanning it
                    // in casterMargin tiles. Relief is measured around the view, not the cover.
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
                        ShadowCasterRing::Grid grid;
                        grid.zoom = ringZoom;
                        bool first = true;
                        for (const vt::TileId& tileId : coverTileIds) {
                            int shift = tileId.zoom - ringZoom;
                            int x = (shift >= 0 ? tileId.x >> shift : tileId.x << -shift);
                            int y = (shift >= 0 ? tileId.y >> shift : tileId.y << -shift);
                            if (first) {
                                grid.minX = grid.maxX = x;
                                grid.minY = grid.maxY = y;
                                first = false;
                            } else {
                                grid.minX = std::min(grid.minX, x); grid.maxX = std::max(grid.maxX, x);
                                grid.minY = std::min(grid.minY, y); grid.maxY = std::max(grid.maxY, y);
                            }
                        }
                        if (!first) {
                            // Bounded: flat ground keeps ringZoom at maxCoverZoom, thousands of tiles a side to the horizon.
                            grid = ShadowCasterRing::fit(grid, casterMargin, MAX_SHADOW_CASTER_TILES);
                            ringZoom = grid.zoom;
                            for (int y = grid.minY - casterMargin; y <= grid.maxY + casterMargin; y++) {
                                for (int x = grid.minX - casterMargin; x <= grid.maxX + casterMargin; x++) {
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
                                // Bounded: subdividing is 4^levels tiles; dropping one only loses a distant shadow.
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
                // The slab must hold the ring casters too, or the near plane truncates a taller ridge's shadow.
                // Each caster's own range too: extruded over the whole slab, a valley tile reaches every cascade.
                std::vector<std::pair<double, double> > casterHeights(casterTileIds.size());
                std::vector<bool> casterHeightKnown(casterTileIds.size(), false);
                if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                    for (std::size_t i = 0; i < casterTileIds.size(); i++) {
                        const vt::TileId& tileId = casterTileIds[i];
                        double casterMin = 0, casterMax = 0;
                        if (elevationManager->getMinMaxDisplayHeightCached(MapTile(tileId.x, tileId.y, tileId.zoom, 0), casterMin, casterMax)) {
                            casterHeights[i] = std::make_pair(casterMin, casterMax);
                            casterHeightKnown[i] = true;
                            if (casterMax > casterMin) {
                                minHeight = std::min(minHeight, casterMin);
                                maxHeight = std::max(maxHeight, casterMax);
                            }
                        }
                    }
                }
                for (std::size_t i = 0; i < casterHeights.size(); i++) {
                    if (!casterHeightKnown[i]) {
                        casterHeights[i] = std::make_pair(minHeight, maxHeight);
                    }
                }
                // One light box per cascade, near slice first; a single box staircases every edge at a tilt.
                int cascades = _terrainShadowMap->getCascades();
                bool boxesValid = true;
                // Per cascade: a near cascade needs only a fraction of the casters.
                std::array<std::vector<vt::TileId>, TerrainShadowMap::MAX_CASCADES> cascadeCasterTiles;
                for (int cascade = 0; cascade < cascades; cascade++) {
                    double depthRangeMeters = 1.0, texelMeters = 0;
                    if (tileLayers.front()->calculateShadowViewProj(coverTileIds, casterTileIds, casterHeights, shadowSunDir, tileHeights, minHeight, maxHeight, lighting.shadowDistance, cglib::length(viewState.getCameraPos() - viewState.getFocusPos()), _terrainShadowMap->getSize(), cascade, cascades, cascadeCasterTiles[cascade], depthRangeMeters, texelMeters, lightViewProjs[cascade])) {
                        // The bias is metric, the shader wants a fraction of the normalised light
                        // depth, and each cascade's box spans its own - so divide per cascade.
                        shadowTexelMeters[cascade] = texelMeters;
                        shadowDepthScales[cascade] = static_cast<float>(1.0 / std::max(1.0, depthRangeMeters));
                    } else if (cascade > 0) {
                        // No ground in this slice: repeat the near box, keeping the atlas layout (a stale box would mis-shadow).
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
                    // The caster pass costs as much as the screen draw: redraw only on a real change, per cascade.
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
                    // Content-driven refreshes are rationed, camera-driven ones are not.
                    if (!refreshAll && contentChanged && _shadowMapAge >= SHADOW_MAP_CONTENT_INTERVAL) {
                        refreshAll = true;
                    }
                    if (!refreshAll && _shadowMapAge >= SHADOW_MAP_MAX_AGE) {
                        refreshAll = true;
                    }
                    // Otherwise per page: each cascade snaps to its own lattice.
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
                                _terrainShadowMap->setCascadeViewport(cascade);
                                if (!refreshAll) {
                                    _terrainShadowMap->clearCascade();
                                }
                                // Every drape layer casts: extrusions may live in any layer (a composite's later style group).
                                for (const std::shared_ptr<TileLayer>& tileLayer : tileLayers) {
                                    bool castGround = (tileLayer == tileLayers.front());
                                    int draws = tileLayer->renderShadowCasters(cascadeCasterTiles[cascade], lightViewProjs[cascade], castGround);
                                    // Ground casters are one draw per tile; the rest are extrusions.
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
                        // The matrices the pages were drawn with, not this frame's fit, or unrefreshed shadows slide.
                        lightViewProjs = _shadowMapViewProjs;
                        shadowTexture = _terrainShadowMap->getTexture();
                        shadowMapSize = _terrainShadowMap->getSize();
                        shadowCascades = cascades;
                        shadowStrength = lighting.shadowStrength;
                        shadowSoftness = lighting.shadowSoftness;
                    }
                } else if (_shadowMapValid && _shadowMapAge < SHADOW_MAP_MAX_AGE) {
                    // A failed fit reuses the last good map, only while recent: older, it shadows buildings no longer drawn.
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
        if (castShadows) {
            std::lock_guard<std::mutex> lock(_shadowSunMutex);
            _shadowSunActive = (shadowTexture != 0);
            _shadowSunDir = shadowSunDir;
        }
        // Logged on change only: shadows going away looks like shadows drawn badly.
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
        // mapbox's u_shadow_fade_range, [far * 0.75, far] against the box cutout; internal units (1 / gl_FragCoord.w).
        cglib::vec2<float> shadowFadeRange(0.0f, 0.0f);
        {
            double distanceFactor = lighting.shadowDistance > 0 ? lighting.shadowDistance : vt::GLTileRenderer::SHADOW_CUTOUT_DISTANCE_FACTOR;
            double cutout = distanceFactor * cglib::length(viewState.getCameraPos() - viewState.getFocusPos());
            if (cutout > 0) {
                shadowFadeRange = cglib::vec2<float>(static_cast<float>(cutout * vt::GLTileRenderer::SHADOW_FADE_START_FRACTION), static_cast<float>(cutout));
            }
        }
        for (const std::shared_ptr<TileLayer>& tileLayer : tileLayers) {
            // The shadow sun: the normal offset scales with the angle the map was rendered from.
            tileLayer->setTerrainShadowMap(shadowTexture, shadowMapSize, shadowCascades, shadowBias, shadowDepthScales, shadowStrength, shadowSoftness, _terrainShadowMap && _terrainShadowMap->isDepthTexture(), _terrainShadowMap && _terrainShadowMap->isHardwarePCF(), lighting.shadowNormalOffset, shadowFadeRange, shadowSunDir, lightViewProjs);
            // The surface draws before each layer's onDrawFrame, which would light it with last frame's sun.
            tileLayer->setTerrainSunLighting(lighting);
        }
        // Resolved once per (reduced) screen pixel: surface draws then fetch once instead of cascading.
        unsigned int maskTexture = 0;
        float invWidth = 0.0f, invHeight = 0.0f;
        if (shadowTexture != 0 && viewState.getWidth() > 0 && viewState.getHeight() > 0) {
            if (!_terrainShadowMaskBuffer) {
                _terrainShadowMaskBuffer = std::make_unique<ScreenMaskBuffer>();
            }
            _terrainShadowMaskBuffer->setSize(viewState.getWidth(), viewState.getHeight(), SHADOW_MASK_DIVISOR);
            FRAME_PROF_GPU_BEGIN(SECTION_SHADOWMASK);
            if (_terrainShadowMaskBuffer->beginPass()) {
                // The first layer alone: the surface is shared.
                tileLayers.front()->renderTerrainShadowMask(coverTileIds);
                _terrainShadowMaskBuffer->endPass(prevFBO, viewState.getWidth(), viewState.getHeight());
                maskTexture = _terrainShadowMaskBuffer->getTexture();
                // Screen size, not the mask's: it maps gl_FragCoord of the full-resolution draw.
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
        // Per layer, then merged: which layers had content tells a full-stack bake from a partial one.
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
        // Seeds cover what the layers' fetching misses after a zoom out; extendSeedsOnly takes only deeper seeds.
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
        // A terrain paint has no tiles: fall back to the terrain's own cover.
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
                    // Nothing else asks for elevation in a paint-only stack.
                    if (coverElevationManager) {
                        MapTile dataTile = coverElevationManager->getDataTile(terrainTile);
                        coverElevationManager->prefetchTileGrid(dataTile, 2);
                        // Keep frames coming until it arrives, or the map idles flat.
                        if (!coverElevationManager->getDataTileGrid(dataTile, ElevationManager::LoadMode::CACHED_ONLY)) {
                            requestRedraw();
                        }
                    }
                }
            }
        }

        // Normalized to a quadtree partition, finest tile wins (04-terrain.md). Ancestors precomputed to
        // avoid an O(n^2) coversTile scan.
        std::set<vt::TileId> collectedAncestors;
        for (auto it = collectedTiles.begin(); it != collectedTiles.end(); it++) {
            for (int zoom = it->first.zoom - 1; zoom >= 0; zoom--) {
                int deltaZoom = it->first.zoom - zoom;
                // Already present means a sibling walked this chain to the root, so the rest is in.
                if (!collectedAncestors.insert(vt::TileId(zoom, it->first.x >> deltaZoom, it->first.y >> deltaZoom)).second) {
                    break;
                }
            }
        }
        auto hasCoarserCollected = [&collectedTiles](const vt::TileId& tileId) {
            for (int zoom = tileId.zoom - 1; zoom >= 0; zoom--) {
                int deltaZoom = tileId.zoom - zoom;
                if (collectedTiles.find(vt::TileId(zoom, tileId.x >> deltaZoom, tileId.y >> deltaZoom)) != collectedTiles.end()) {
                    return true;
                }
            }
            return false;
        };

        std::vector<vt::TileId> pending;
        for (auto it = collectedTiles.begin(); it != collectedTiles.end(); it++) {
            if (!hasCoarserCollected(it->first)) {
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
        // Capped at what the camera shows: blending-out tiles from before a zoom out would drag it finer.
        // The render zoom, which tiles are picked at: the app's number is a level short with ZoomOffset 1 (web).
        int viewZoomCap = static_cast<int>(std::ceil(viewState.getRenderZoom())) + 1;
        coverZoom = std::min(maxCollectedZoom, std::max(viewZoomCap, minTopZoom));
        // Split only where a finer collected tile sits inside, or leaves explode (04-terrain.md).
        std::vector<vt::TileId> tops = pending;
        auto buildLeaves = [&](int zoomLimit) {
            leaves.clear();
            std::vector<vt::TileId> stack = tops;
            while (!stack.empty() && leaves.size() + stack.size() <= MAX_DRAPE_TILES) {
                vt::TileId tileId = stack.back();
                stack.pop_back();
                bool finerInside = collectedAncestors.find(tileId) != collectedAncestors.end();
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

        // Post-processing opt-outs are overlays, kept out of the whole terrain arrangement below.
        _overlayLayers.clear();
        if (postProcessing) {
            auto overlay = std::stable_partition(layers.begin(), layers.end(), [](const std::shared_ptr<Layer>& layer) {
                return layer->isPostProcessed();
            });
            _overlayLayers.assign(overlay, layers.end());
            layers.erase(overlay, layers.end());
        }

        // The first suitable tile layer writes the terrain depth, bit-exact; else an approximate pre-pass.
        bool terrainMode = false;
        {
            // _elevationTextureCache is not released here: a renderer may still draw from it.
            if (auto terrainOptions = _options->getTerrainOptions()) {
                if (terrainOptions->isActive()) {
                    terrainMode = true;
                    // Every frame: a projection switch replaces the transformer, not the renderer.
                    if (_terrainRenderer) {
                        _terrainRenderer->setTileTransformer(_options->getTileTransformer());
                    }
                    // Elevation lands off-frame; without this a still map sits on a half-displaced mesh.
                    if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                        // Prefetch origin: the focus, as the ground under a tilted camera is off screen.
                        const cglib::vec3<double>& focusPos = viewState.getFocusPos();
                        elevationManager->setPrefetchFocus(focusPos(0), focusPos(1));
                        // Once a frame for all layers.
                        if (auto elevationTextureCache = getElevationTextureCache(elevationManager)) {
                            // Before the frame's uploads: a new reach re-pads every texture.
                            elevationTextureCache->setBorderMetres(terrainOptions->getNormalSampleDistance());
                            elevationTextureCache->beginFrame(viewState.getZoom());
                        }
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
                            // A labels-only layer draws no ground, so it cannot be the depth writer.
                            bool depthWrite = !depthWriteAssigned && tileLayer->isVisible() && tileLayer->getOpacity() >= 1.0f && tileLayer->hasGroundContent();
                            tileLayer->setTerrainDepthWriteMode(depthWrite);
                            tileLayer->setTerrainRenderOrder(terrainRenderOrder++);
                            depthWriteAssigned = depthWriteAssigned || depthWrite;
                        }
                    }
                    // Base fill before all layers; color-only under a depth-writing layer, else the depth source.
                    FRAME_PROF_ADD(preHeadMs, profDrawStart);
                    bool depthSourceRendered = false;
                    {
                        FRAME_PROF_NOW(profTerrainStart);
                        if (!_terrainRenderer) {
                            _terrainRenderer = std::make_unique<TerrainRenderer>();
                    _terrainRenderer->setTileTransformer(_options->getTileTransformer());
                        }
                        bool keepDepth = !depthWriteAssigned;
                        bool backgroundRendered = false;
                        // A surface shader overrides the bitmap/color fill, with the resolved sun and fog.
                        if (!terrainOptions->getSurfaceShaderSource().empty()) {
                            // The surface may be the only content, so it drives the elevation loads itself.
                            if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                                FRAME_PROF_NOW(profCutStart);
                                std::vector<MapTile> terrainTiles;
                                _terrainRenderer->collectVisibleTiles(viewState, terrainOptions, terrainTiles);
                                FRAME_PROF_ADD(preTerrainCutMs, profCutStart);
                                FRAME_PROF_NOW(profPrefetchStart);
                                for (const MapTile& terrainTile : terrainTiles) {
                                    MapTile dataTile = elevationManager->getDataTile(terrainTile);
                                    elevationManager->prefetchTileGrid(dataTile, 2);
                                    if (!elevationManager->getDataTileGrid(dataTile, ElevationManager::LoadMode::CACHED_ONLY)) {
                                        requestRedraw();
                                    }
                                }
                                FRAME_PROF_ADD(preTerrainPrefetchMs, profPrefetchStart);
                            }
                            ResolvedLighting surfaceLighting = resolveLighting(_options->getLightOptions(), _frameStyleEnvironment);
                            FRAME_PROF_NOW(profSurfaceStart);
                            // Per-fragment normals from the GPU elevation textures.
                            if (std::shared_ptr<ElevationManager> surfaceElevation = terrainOptions->getElevationManager()) {
                                _terrainRenderer->setElevationTextureCache(getElevationTextureCache(surfaceElevation));
                            }
                            backgroundRendered = _terrainRenderer->renderSurface(viewState, terrainOptions, _glResourceManager, surfaceLighting, _frameFog, keepDepth);
                            FRAME_PROF_ADD(preTerrainSurfaceMs, profSurfaceStart);
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
                        FRAME_PROF_ADD(preTerrainMs, profTerrainStart);
                    }
                    if (terrainOptions->isBillboardOcclusionEnabled()) {
                        FRAME_PROF_NOW(profDepthStart);
                        // Terrain depth for label/billboard occlusion.
                        if (!_terrainRenderer) {
                            _terrainRenderer = std::make_unique<TerrainRenderer>();
                    _terrainRenderer->setTileTransformer(_options->getTileTransformer());
                        }
                        _terrainRenderer->updateDepthBuffer(viewState, terrainOptions, _glResourceManager);
                        if (_terrainRenderer->isDepthBufferStale()) {
                            // Deferred while moving; keep asking so it runs once the camera settles.
                            requestRedraw();
                        }
                        FRAME_PROF_ADD(preDepthMs, profDepthStart);
                    }

                    // A bound on the zoom, not a corrective event, which would oscillate (mapbox's _constrainCamera,
                    // docs/internals/rendering/04-terrain.md).
                    {
                        FRAME_PROF_NOW(profClearanceStart);
                        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
                        // Through the surface: on a globe an orbit is a world length, a height internal (2x apart).
                        std::shared_ptr<ProjectionSurface> clearanceSurface = _options->getProjectionSurface();
                        MapPos cameraMapPos = (clearanceSurface ? clearanceSurface->calculateMapPos(viewState.getCameraPos()) : MapPos());
                        double worldPerInternalZ = viewState.worldPerInternal();
                        double displayScale = elevationManager->getDisplayScale(cameraMapPos.getY());
                        double terrainZ = elevationManager->getDisplayHeight(cameraMapPos.getX(), cameraMapPos.getY(), ElevationManager::LoadMode::CACHED_ONLY);
                        double clearanceFloor = terrainOptions->getCameraClearance() * displayScale;
                        if (clearanceSurface) {
                            std::lock_guard<std::recursive_mutex> lock(_mutex);
                            _viewState.setTerrainCameraReference(terrainZ, clearanceFloor, terrainOptions->getCameraClearanceFraction());
                        }
                        FRAME_PROF_ADD(preClearanceMs, profClearanceStart);
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

        // Cross-layer draping: one baked texture per terrain tile, so draped content never depth-fights the surface.

        // Whether the drape carried the ground, so the contact shadow can stand down.
        bool groundAODraped = false;
        std::vector<std::shared_ptr<TileLayer> > drapeLayers;
        bool sharedGroundActive = false;
        // Whether a terrain branch closed out preludeMs, to avoid double-counting.
        bool preludeAccounted = false;
        if (terrainMode) {
            // Without a drape a terrain paint draws on the terrain's own cover; a baking paint ignores it.
            if (auto paintTerrainOptions = _options->getTerrainOptions()) {
                FRAME_PROF_NOW(profPaintStart);
                std::vector<std::shared_ptr<TileLayer> > paintLayers;
                for (const std::shared_ptr<Layer>& layer : layers) {
                    layer->collectDrapeLayers(paintLayers, viewState);
                }
                FRAME_PROF_ADD(prePaintLayersMs, profPaintStart);
                bool anyPaint = false;
                for (const std::shared_ptr<TileLayer>& tileLayer : paintLayers) {
                    anyPaint = anyPaint || tileLayer->paintsEveryDrapeTile();
                }
                if (anyPaint && _terrainRenderer) {
                    FRAME_PROF_NOW(profPaintCoverStart);
                    std::vector<MapTile> terrainTiles;
                    _terrainRenderer->collectVisibleTiles(viewState, paintTerrainOptions, terrainTiles);
                    std::vector<vt::TileId> paintTileIds;
                    paintTileIds.reserve(terrainTiles.size());
                    for (const MapTile& terrainTile : terrainTiles) {
                        paintTileIds.emplace_back(terrainTile.getZoom(), terrainTile.getX(), terrainTile.getY());
                    }
                    FRAME_PROF_ADD(prePaintCoverMs, profPaintCoverStart);
                    FRAME_PROF_NOW(profPaintPushStart);
                    for (const std::shared_ptr<TileLayer>& tileLayer : paintLayers) {
                        if (tileLayer->paintsEveryDrapeTile()) {
                            tileLayer->setTerrainPaintTiles(paintTileIds);
                        }
                    }
                    FRAME_PROF_ADD(prePaintPushMs, profPaintPushStart);
                }
                FRAME_PROF_ADD(prePaintMs, profPaintStart);
            }
            FRAME_PROF_NOW(profTailStart);
            if (auto terrainOptions = _options->getTerrainOptions()) {
                FRAME_PROF_ADD(preTailOptionsMs, profTailStart);
                FRAME_PROF_NOW(profTailWalkStart);
                if (terrainOptions->isDrapeFillsEnabled()) {
                    // Composite layers contribute their children, in draw order.
                    for (const std::shared_ptr<Layer>& layer : layers) {
                        layer->collectDrapeLayers(drapeLayers, viewState);
                    }
                    FRAME_PROF_ADD(preTailWalkMs, profTailWalkStart);
                } else {
                    // No drape (tangram): one shared cover, ground drawn once, layers composited straight onto it.
                    std::vector<std::shared_ptr<TileLayer> > groundLayers;
                    for (const std::shared_ptr<Layer>& layer : layers) {
                        layer->collectDrapeLayers(groundLayers, viewState);
                    }
                    FRAME_PROF_ADD(preTailWalkMs, profTailWalkStart);
                    if (!groundLayers.empty()) {
                        // Every layer's render tiles must exist before the cover is read from them.
                        FRAME_PROF_ADD(preTailMs, profTailStart);
                        FRAME_PROF_ADD(preludeMs, profDrawStart);
                        preludeAccounted = true;
                        FRAME_PROF_NOW(profPrepareStart);
                        FRAME_PROF_GPU_BEGIN(SECTION_PREPARE);
                        for (const std::shared_ptr<TileLayer>& tileLayer : groundLayers) {
                            tileLayer->prepareTerrainDrapeFrame(deltaSeconds, viewState);
                        }
                        FRAME_PROF_ADD(prepareMs, profPrepareStart);
                        FRAME_PROF_NOW(profCoverStart);
                        FRAME_PROF_GPU_BEGIN(SECTION_COVER);

                        // Seeded by what the camera sees, not what the layers fetched.
                        FRAME_PROF_NOW(profCoverSeedStart);
                        std::vector<vt::TileId> terrainCoverTileIds = collectTerrainCoverTileIds(viewState, terrainOptions);
                        FRAME_PROF_ADD(coverSeedMs, profCoverSeedStart);
                        std::vector<std::map<vt::TileId, std::size_t> > groundLayerTiles;
                        std::map<vt::TileId, std::size_t> groundCollectedTiles;
                        std::vector<vt::TileId> groundTileIds;
                        std::vector<int> groundProxyDepths;
                        std::vector<bool> groundStandingIn; // parallel: this tile is drawn in place of a finer one
                        int groundZoom = 0, groundMaxCollectedZoom = 0;
                        FRAME_PROF_NOW(profCoverCollectStart);
                        collectTerrainCover(groundLayers, viewState, terrainOptions, terrainCoverTileIds, false, groundLayerTiles, groundCollectedTiles, groundTileIds, groundZoom, groundMaxCollectedZoom);
                        FRAME_PROF_ADD(coverCollectMs, profCoverCollectStart);

                        FRAME_PROF_NOW(profCoverStandInStart);
                        // A leaf without DEM would flash flat and bare: stand on the coarsest loaded ancestor.
                        if (std::shared_ptr<ElevationManager> groundElevationManager = terrainOptions->getElevationManager()) {
                            // Neighbouring leaves share most of their ancestor chains.
                            std::map<vt::TileId, bool> elevationMemo;
                            auto hasElevation = [&groundElevationManager, &elevationMemo](const vt::TileId& tileId) {
                                auto memo = elevationMemo.find(tileId);
                                if (memo != elevationMemo.end()) {
                                    return memo->second;
                                }
                                int tileMask = (1 << tileId.zoom) - 1;
                                MapTile mapTile(tileId.x & tileMask, std::min(std::max(tileId.y, 0), tileMask), tileId.zoom, 0);
                                bool loaded = static_cast<bool>(groundElevationManager->getTileGrid(mapTile, ElevationManager::LoadMode::CACHED_ONLY));
                                elevationMemo.emplace(tileId, loaded);
                                return loaded;
                            };
                            std::vector<vt::TileId> loadedTileIds;
                            std::vector<bool> standingIn;
                            // Each stand-in's index in loadedTileIds, so the dedup is a lookup, not a quadratic scan.
                            std::map<vt::TileId, std::size_t> loadedIndex;
                            loadedTileIds.reserve(groundTileIds.size());
                            standingIn.reserve(groundTileIds.size());
                            for (const vt::TileId& tileId : groundTileIds) {
                                vt::TileId standIn = tileId;
                                while (standIn.zoom > 0 && !hasElevation(standIn)) {
                                    standIn = standIn.getParent();
                                }
                                // The walk can bring several leaves onto one ancestor; drawing it
                                // once is both correct and cheaper.
                                auto it = loadedIndex.find(standIn);
                                if (it == loadedIndex.end()) {
                                    loadedIndex.emplace(standIn, loadedTileIds.size());
                                    loadedTileIds.push_back(standIn);
                                    standingIn.push_back(standIn != tileId);
                                } else if (standIn != tileId) {
                                    standingIn[it->second] = true;
                                }
                            }
                            groundTileIds = std::move(loadedTileIds);
                            groundStandingIn = std::move(standingIn);
                        }
                        FRAME_PROF_ADD(coverStandInMs, profCoverStandInStart);

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

                        // Unpainted ground would show the plane behind the terrain: terrain background, else the first layer's.
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

                        // Dense ordinals per layer from 1 (0 is the ground): the total span sets the leak threshold (05-depth-model.md).
                        int ordinalBase = 1;
                        for (const std::shared_ptr<TileLayer>& tileLayer : groundLayers) {
                            tileLayer->setExternalDrapeTarget(false);
                            tileLayer->setExternalDrapeTiles(std::vector<vt::TileId>());
                            tileLayer->setTerrainGroundTiles(groundTileIds, groundProxyDepths);
                            tileLayer->setTerrainLayerOrdinalBase(ordinalBase);
                            ordinalBase += std::max(1, tileLayer->getStyleLayerCount());
                        }
                        // Before the ground draws; with no bake, content-driven refresh rides on the cover changing.
                        GLint groundPrevFBO = 0;
                        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &groundPrevFBO);
                        ResolvedLighting lighting;
                        std::array<double, TerrainShadowMap::MAX_CASCADES> shadowTexelMeters = { };
                        bool coverChanged = (_groundCoverTileIds != groundTileIds);
                        _groundCoverTileIds = groundTileIds;
                        // castShadows false: the road overlay shows acne here that the drape path does not.
                        FRAME_PROF_NOW(profCoverShadowStart);
                        applyTerrainShadows(groundLayers, groundTileIds, terrainOptions, viewState, groundPrevFBO, coverChanged, false, lighting, shadowTexelMeters);
                        FRAME_PROF_ADD(coverShadowMs, profCoverShadowStart);

                        FRAME_PROF_ADD(coverMs, profCoverStart);
                        FRAME_PROF_NOW(profGroundStart);
                        FRAME_PROF_GPU_BEGIN(SECTION_DRAPE);
                        // The painting layer draws the ground (its renderer holds the paint), else the first layer.
                        std::shared_ptr<TileLayer> groundDrawer = groundLayers.front();
                        for (const std::shared_ptr<TileLayer>& tileLayer : groundLayers) {
                            if (tileLayer->paintsEveryDrapeTile()) {
                                groundDrawer = tileLayer;
                                break;
                            }
                        }
                        // Skipped when the surface shader already painted this ground (TerrainOptions::
                        // setSharedGroundEnabled); the ground tiles and ordinals above are still published.
                        int groundDraws = 0;
                        if (terrainOptions->isSharedGroundEnabled()) {
                            groundDraws = groundDrawer->renderTerrainGround(groundColor);
                        }
                        FRAME_PROF_ADD(drapeMs, profGroundStart);
                        FRAME_PROF_GPU_END();

                        sharedGroundActive = true;
                        // Also logged on the first frame with a cover: a settled map stops drawing frames.
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
                // A single stack for now; several only matter with a non-drapeable layer between drapeable ones.
                if (!drapeLayers.empty()) {
                    FRAME_PROF_NOW(profTailCacheStart);
                    if (!_terrainDrapeCache) {
                        _terrainDrapeCache = std::make_unique<TerrainDrapeCache>();
                    }
                    _terrainDrapeCache->setMaxBytes(static_cast<std::size_t>(std::max(0, terrainOptions->getDrapeCacheSize())) * 1024 * 1024);
                    _terrainDrapeCache->setResolution(TileRenderer::resolveDrapeResolution(terrainOptions->getDrapeResolution(), viewState, _options,
                        static_cast<std::size_t>(terrainOptions->getDrapeCacheSize()), terrainOptions->getDrapeWorkingSet()));
                    // Which layers bake: a style switch builds new layers. Non-tile content folds in its appearance.
                    std::size_t stackSignature = 0;
                    for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                        std::size_t layerHash = tileLayer->drapeStackSignature();
                        stackSignature ^= layerHash + 0x9e3779b9 + (stackSignature << 6) + (stackSignature >> 2);
                    }
                    _terrainDrapeCache->setStackSignature(stackSignature);
                    FRAME_PROF_ADD(preTailCacheMs, profTailCacheStart);

                    // Every layer's render tiles must exist before any bakes.
                    FRAME_PROF_ADD(preTailMs, profTailStart);
                    FRAME_PROF_ADD(preludeMs, profDrawStart);
                    preludeAccounted = true;
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
                    // Camera seeds split past a source's maxzoom, or the ground stays soft (mapbox's proxy source).
                    collectTerrainCover(drapeLayers, viewState, terrainOptions, collectTerrainCoverTileIds(viewState, terrainOptions), true, layerTiles, collectedTiles, leaves, drapeZoom, maxCollectedZoom);
                    std::map<vt::TileId, std::size_t> drapeTiles;
                    // Against bakedLayerMask: separates a moved-on picture from a missing layer.
                    std::map<vt::TileId, std::size_t> drapeTileLayerMasks;
                    // A paint needs decoded DEM; via the fingerprint the tile re-bakes once it arrives.
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
                            // Non-tile content cannot join the mask (incomplete tiles are not drawn); it gets a re-bake instead.
                            if (drapeLayers[i]->paintsEveryDrapeTile()) {
                                continue;
                            }
                            for (auto it = layerTiles[i].begin(); it != layerTiles[i].end(); it++) {
                                if (it->second == 0) {
                                    continue; // reported for the cover, but nothing drapeable in it
                                }
                                // Only what bakeDrapeTile draws: its own tile or a coarser one.
                                if (it->first == tileId || coversTile(it->first, tileId)) {
                                    layerMask |= static_cast<std::size_t>(1) << i;
                                    break;
                                }
                            }
                        }
                        drapeTileLayerMasks[tileId] = layerMask;
                        // Every contributor, including coarser and (at the split cap) finer tiles, or content stays stale.
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
                            // Paintability is per tile: the tile re-bakes the moment its elevation arrives.
                            std::size_t elevationTerm = (paintable ? 0x9e3779b9u : 0x85ebca6bu);
                            fingerprint ^= elevationTerm + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2);
                        }
                        // Style functions use the view zoom; the quantised term only follows a settled camera.
                        fingerprint ^= _drapeBakeZoomTerm + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2);
                        drapeTiles[tileId] = fingerprint;
                    }

                    // A texel per screen pixel per leaf, the cover fitting half the cache (the other half holds the
                    // generation stand-ins read). An app's DrapeResolution keeps one size for all.
                    std::map<vt::TileId, int> leafResolution;
                    if (terrainOptions->getDrapeResolution() <= 0 && TerrainDrapeCache::isBudgetEnabled() && std::dynamic_pointer_cast<PlanarProjectionSurface>(_options->getProjectionSurface())) {
                        const cglib::mat4x4<double>& mvp = viewState.getModelviewProjectionMat();
                        double groundZ = viewState.getFocusPos()(2);
                        std::vector<double> edgePixels;
                        std::vector<vt::TileId> sizedLeaves;
                        for (auto it = drapeTiles.begin(); it != drapeTiles.end(); it++) {
                            const vt::TileId& tileId = it->first;
                            double extent = static_cast<double>(1 << tileId.zoom);
                            // Screen pixels per tile width where the leaf is ON screen; the ground under the camera is nearer
                            // than anything drawn. A leaf seen only in the margin (a sliver between samples) takes its nearest scale.
                            double marginX = 0.25 * viewState.getWidth(), marginY = 0.25 * viewState.getHeight();
                            auto project = [&](double u, double v, cglib::vec2<double>& screen) {
                                double x = ((tileId.x + u) / extent - 0.5) * Const::WORLD_SIZE;
                                double y = (0.5 - (tileId.y + v) / extent) * Const::WORLD_SIZE;
                                cglib::vec4<double> clip = cglib::transform(cglib::vec4<double>(x, y, groundZ, 1.0), mvp);
                                if (!(clip(3) > 1.0e-9)) {
                                    return false;
                                }
                                screen = cglib::vec2<double>((clip(0) / clip(3) + 1.0) * 0.5 * viewState.getWidth(), (clip(1) / clip(3) + 1.0) * 0.5 * viewState.getHeight());
                                return true;
                            };
                            static const int SAMPLES = 9;
                            static const double STEP = 0.01;
                            double edge = 0;
                            double margin = std::numeric_limits<double>::max();
                            for (int i = 0; i < SAMPLES; i++) {
                                for (int j = 0; j < SAMPLES; j++) {
                                    double u = (i + 0.5) / SAMPLES, v = (j + 0.5) / SAMPLES;
                                    cglib::vec2<double> p, pu, pv;
                                    if (!project(u, v, p) || !project(u + STEP, v, pu) || !project(u, v + STEP, pv) || std::abs(p(0) - 0.5 * viewState.getWidth()) > 0.5 * viewState.getWidth() + marginX || std::abs(p(1) - 0.5 * viewState.getHeight()) > 0.5 * viewState.getHeight() + marginY) {
                                        continue;
                                    }
                                    double scale = std::max(cglib::length(pu - p), cglib::length(pv - p)) / STEP;
                                    if (p(0) >= 0 && p(0) <= viewState.getWidth() && p(1) >= 0 && p(1) <= viewState.getHeight()) {
                                        edge = std::max(edge, scale);
                                    } else {
                                        margin = std::min(margin, scale);
                                    }
                                }
                            }
                            if (edge == 0 && margin < std::numeric_limits<double>::max()) {
                                edge = margin;
                            }
                            edgePixels.push_back(edge);
                            sizedLeaves.push_back(tileId);
                        }
                        std::size_t cacheBytes = (terrainOptions->getDrapeCacheSize() > 0 ? static_cast<std::size_t>(terrainOptions->getDrapeCacheSize()) * 1024 * 1024 : TerrainDrapeCache::MAX_BYTES);
                        std::vector<int> sizes = DrapeTuning::leafResolutions(edgePixels, cacheBytes / 2, TileRenderer::MIN_DRAPE_RESOLUTION, TileRenderer::MAX_DRAPE_RESOLUTION);
                        for (std::size_t i = 0; i < sizedLeaves.size(); i++) {
                            leafResolution[sizedLeaves[i]] = sizes[i];
                        }
                    }
                    auto resolutionOf = [&leafResolution](const vt::TileId& tileId) {
                        auto it = leafResolution.find(tileId);
                        return it != leafResolution.end() ? it->second : 0;
                    };

                    // External targets suppress each layer's surface: only with tiles to drape.
                    bool drapeActive = !drapeTiles.empty();
                    std::vector<vt::TileId> drapeTileIds;
                    drapeTileIds.reserve(drapeTiles.size());
                    for (auto it = drapeTiles.begin(); it != drapeTiles.end(); it++) {
                        drapeTileIds.push_back(it->first);
                    }
                    for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                        tileLayer->setExternalDrapeTarget(drapeActive);
                        // Explicit hand-off: startFrame runs between the surface and layer draws and resets frame state.
                        tileLayer->setExternalDrapeTiles(drapeActive ? drapeTileIds : std::vector<vt::TileId>());
                    }
                    // Live units with draped units after them are masked (#175); rule in terrain/DrapeStackCuts.h.
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

                    // Baked-in background colour, or unpainted texels show the plane behind the terrain.
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
                    // Leaves are sized one by one, so the viewport follows the texture being written.
                    auto bindDrapeTarget = [&](unsigned int texture, int size) {
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
                        glViewport(0, 0, size, size);
                    };
                    // Resolved once, not per tile: it locks two mutexes per layer.
                    bool groundAOWanted = false;
                    for (const std::shared_ptr<TileLayer>& tileLayer : drapeLayers) {
                        groundAOWanted = tileLayer->isGroundAOBakeable() || groundAOWanted;
                    }
                    static int bakedTiles = 0, bakedPrimitives = 0;
                    int surfaceDraws = 0, filledSurfaces = 0, skippedSurfaces = 0;
                    // Per frame, for the shadow cache.
                    int bakedThisFrame = 0;
                    // Per-frame bake budget by urgency class (docs/internals/rendering/04-terrain.md).
                    static const int DRAPE_BAKE_BUDGET_BLANK = 8;
                    static const int DRAPE_BAKE_BUDGET_STANDIN = 3;
                    // Missing a whole layer (rasters decode first): too slow reads as a hillshade flash.
                    static const int DRAPE_BAKE_BUDGET_PARTIAL = 6;
                    static const int DRAPE_BAKE_BUDGET_STALE = 1;
                    // Baked from a replaced layer stack: shows the previous map, so cleared at the blank-tile rate.
                    static const int DRAPE_BAKE_BUDGET_RESTACK = 8;
                    // Deck-sized span bakes let through before the time budget, or decks dress one per frame.
                    static const int DRAPE_BAKE_BUDGET_SPAN = 3;
                    // Wall-clock ceiling for all classes; a still camera gets far more room than a moving one.
                    static const double DRAPE_BAKE_TIME_BUDGET = 16.0;       // ms, camera moving
                    static const double DRAPE_BAKE_TIME_BUDGET_STILL = 60.0; // ms, camera at rest
                    // The moving budget outlives the move by this; debug.massif.drapesettle <ms> overrides it (demo builds).
                    static const double DRAPE_BAKE_SETTLE_MS = [] {
                        double settle = 300.0;
#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
                        char property[PROP_VALUE_MAX] = { 0 };
                        if (__system_property_get("debug.massif.drapesettle", property) > 0) {
                            settle = std::atof(property);
                        }
#endif
                        return settle;
                    }();
                    struct BakeRequest { vt::TileId tileId; std::size_t fingerprint; std::size_t drapedIndex; };
                    std::vector<BakeRequest> blankTiles, standInTiles, partialTiles, staleTiles, restackTiles;

                    // Without elevation, stand on the previous generation or draw nothing: a false ground writes depth.
                    // Flat only when nothing has elevation (04-terrain.md).
                    int displacedLeaves = 0;
                    for (auto it = drapeTiles.begin(); it != drapeTiles.end(); it++) {
                        displacedLeaves += leafElevation[it->first] ? 1 : 0;
                    }
                    bool sceneDisplaced = displacedLeaves > 0;

                    // Seeding copies cached tiles into a new texture (a few quads, not a bake); seeds are never sources.
                    static const int DRAPE_SEED_BUDGET = 16;
                    int seedBudget = DRAPE_SEED_BUDGET;
                    int seededTiles = 0;
                    struct SeedSource { unsigned int texture; float dstX, dstY, dstScale, uvX, uvY, uvScale; };
                    auto seedTile = [&](const vt::TileId& tileId, unsigned int texture) {
                        if (seedBudget <= 0 || texture == 0) {
                            return false;
                        }
                        std::vector<SeedSource> sources;
                        // Finer tiles first: just replaced, full detail, tiling this one exactly.
                        for (const std::pair<vt::TileId, unsigned int>& descendant : _terrainDrapeCache->findBakedDescendants(tileId, 0)) {
                            int levels = descendant.first.zoom - tileId.zoom;
                            int span = 1 << levels;
                            int ix = descendant.first.x - (tileId.x << levels);
                            int iy = descendant.first.y - (tileId.y << levels);
                            // Mirrored y: texture v runs north, the XYZ tile y runs south.
                            sources.push_back(SeedSource { descendant.second, static_cast<float>(ix) / span, static_cast<float>(span - 1 - iy) / span, 1.0f / span, 0.0f, 0.0f, 1.0f });
                        }
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
                        bindDrapeTarget(texture, _terrainDrapeCache->getTextureResolution(tileId, 0));
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
                        unsigned int replaced = 0;
                        int replacedSize = _terrainDrapeCache->getTextureResolution(it->first, 0);
                        unsigned int texture = _terrainDrapeCache->acquire(it->first, 0, it->second, resolutionOf(it->first), &replaced, needsBake, hasContent);
                        if (replaced != 0) {
                            // Resized: the old picture carried across, so the leaf sharpens without a blank frame.
                            beginOffscreen();
                            bindDrapeTarget(texture, _terrainDrapeCache->getTextureResolution(it->first, 0));
                            glClearColor(drapeClearColor.getR() / 255.0f, drapeClearColor.getG() / 255.0f, drapeClearColor.getB() / 255.0f, drapeClearColor.getA() / 255.0f);
                            glClear(GL_COLOR_BUFFER_BIT);
                            drapeLayers.front()->blitDrapeTexture(replaced, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
                            TerrainDrapeCache::generateMipmaps(texture);
                            _terrainDrapeCache->recycle(replaced, false, replacedSize);
                            _terrainDrapeCache->markSeeded(it->first, 0);
                            hasContent = true;
                        }
                        bool fingerprintStale = needsBake;
                        if (!hasContent && seedTile(it->first, texture)) {
                            _terrainDrapeCache->markSeeded(it->first, 0);
                            hasContent = true;
                        }
                        bool baked = _terrainDrapeCache->isBaked(it->first, 0);
                        // Usable = every layer with content is baked in; the first bake after a zoom out may hold rasters only.
                        std::size_t wantedMask = drapeTileLayerMasks[it->first];
                        std::size_t bakedMask = _terrainDrapeCache->bakedLayerMask(it->first, 0);
                        bool complete = DrapeStandIn::isComplete(baked, wantedMask, bakedMask);
                        // A seed already is the finer generation.
                        bool showsStandIn = hasContent && !baked;
                        // No content: the recycled texture holds another tile. Stand in on the nearest baked ancestor.
                        DrapedTile draped { it->first, hasContent ? texture : 0u, 0.0f, 0.0f, 1.0f };
                        if (!hasContent) {
                            vt::TileId ancestor = it->first;
                            float offsetX = 0.0f, offsetY = 0.0f, scale = 1.0f;
                            for (int level = 0; level < 6 && ancestor.zoom > 0; level++) {
                                // Mirrored y: tile-local y runs north, XYZ tile y south.
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
                        // No DEM in a displaced scene: skip its false flat surface; its descendants still draw.
                        bool skipSurface = sceneDisplaced && !leafElevation[it->first];
                        std::size_t drapedIndex = std::numeric_limits<std::size_t>::max(); // never indexes the list
                        if (!skipSurface) {
                            drapedIndex = drapedTiles.size(); // before the stand-in draws below extend the list
                            drapedTiles.push_back(draped);
                        } else {
                            skippedSurfaces++;
                        }
                        // Ancestor sub-rects beat descendants: same mesh, blurrier texture; finer meshes sit off the terrain.
                        bool showsAncestor = !hasContent && draped.texture != 0 && !skipSurface;
                        // Same for a leaf's own incomplete bake: stacking finer tiles over it pops the mesh.
                        bool showsOwnBake = hasContent && baked && !skipSurface;
                        if (((!complete && !showsStandIn) || skipSurface) && !showsAncestor && !showsOwnBake) {
                            // After this tile's own entry: coinciding surfaces, the later draw wins.
                            std::vector<std::pair<vt::TileId, unsigned int>> descendants = _terrainDrapeCache->findBakedDescendants(it->first, 0);
                            for (std::size_t i = 0; i < descendants.size(); i++) {
                                const vt::TileId& descendantTileId = descendants[i].first;
                                // An unusable descendant does not rule out its own descendants.
                                bool usable = !(sceneDisplaced && !hasElevationData(descendantTileId)) // no ground to put the picture on
                                    && (wantedMask & ~_terrainDrapeCache->bakedLayerMask(descendantTileId, 0)) == 0; // as incomplete as the tile it stands in for
                                if (!usable) {
                                    std::vector<std::pair<vt::TileId, unsigned int>> finer = _terrainDrapeCache->findBakedDescendants(descendantTileId, 0);
                                    descendants.insert(descendants.end(), finer.begin(), finer.end());
                                    continue;
                                }
                                drapedTiles.push_back(DrapedTile { descendantTileId, descendants[i].second, 0.0f, 0.0f, 1.0f });
                            }
                        }
                        // A separately evicted mask would stay missing for good: re-bake the tile.
                        for (std::size_t k = 0; k < drapeCuts.size() && !needsBake; k++) {
                            needsBake = !_terrainDrapeCache->isBaked(it->first, static_cast<int>(k) + 1);
                        }
                        if (!needsBake) {
                            continue;
                        }
                        if (fingerprintStale) {
                            VT_STAT_INC(drapeStaleFingerprint);
                        } else {
                            VT_STAT_INC(drapeStaleMask);
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
                    // Nearest the focus first within each class, in tile lengths of each tile's own zoom.
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
                    // Bridge/tunnel-only tiles. Negative: stack 0 is the ground drape, > 0 are R8 masks.
                    const int SPAN_DRAPE_STACK = -1;
                    std::map<vt::TileId, std::size_t> spanDrapeTiles;
                    for (std::size_t i = 0; i < drapeLayers.size(); i++) {
                        drapeLayers[i]->collectSpanDrapeTiles(spanDrapeTiles);
                    }
                    auto bakeTile = [&](const BakeRequest& request) {
                        bool needsBake = false, hasContent = false;
                        unsigned int texture = _terrainDrapeCache->acquire(request.tileId, 0, request.fingerprint, resolutionOf(request.tileId), nullptr, needsBake, hasContent);
                        int tileResolution = _terrainDrapeCache->getTextureResolution(request.tileId, 0);
                        beginOffscreen();
                        bindDrapeTarget(texture, tileResolution);
                        glClearColor(drapeClearColor.getR() / 255.0f, drapeClearColor.getG() / 255.0f, drapeClearColor.getB() / 255.0f, drapeClearColor.getA() / 255.0f);
                        glClear(GL_COLOR_BUFFER_BIT);
                        // Later layers composite over earlier ones: the owner clears, the bakers do not.
                        std::size_t bakedMask = 0;
                        for (std::size_t i = 0; i < drapeLayers.size(); i++) {
                            int primitives = drapeLayers[i]->bakeDrapeTile(request.tileId);
                            bakedPrimitives += primitives;
                            if (primitives > 0 && i < sizeof(std::size_t) * 8) {
                                bakedMask |= static_cast<std::size_t>(1) << i;
                            }
                        }
                        // What went in, not what was asked. An unpainted tile's fingerprint cannot match, so it re-bakes.
                        std::size_t bakedFingerprint = request.fingerprint;
                        for (std::size_t i = 0; i < drapeLayers.size() && i < sizeof(std::size_t) * 8; i++) {
                            if (drapeLayers[i]->paintsEveryDrapeTile() && (bakedMask & (static_cast<std::size_t>(1) << i)) == 0) {
                                // Only when the texture is merely not uploaded yet, or an unpaintable tile re-bakes forever.
                                auto leafElevationIt = leafElevation.find(request.tileId);
                                if (leafElevationIt != leafElevation.end() && leafElevationIt->second) {
                                    bakedFingerprint = ~request.fingerprint;
                                }
                                break;
                            }
                        }
                        // Contact shadows resolved under MIN into the drape; restores all GL state (the bake matrix has no y flip).
                        if (groundAOWanted) {
                            if (!_groundAODrapeBuffer) {
                                _groundAODrapeBuffer = std::make_unique<ScreenMaskBuffer>(false);
                            }
                            _groundAODrapeBuffer->setSize(tileResolution, tileResolution, 1);
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
                                _groundAODrapeBuffer->endPassRaw(drapeFBO, tileResolution, tileResolution);
                                if (aoBaked > 0) {
                                    // Premultiplied drape; the mask's alpha is 1, so dst alpha is untouched.
                                    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
                                    drawMaskQuad(_groundAODrapeBuffer->getTexture(), 1.0f / tileResolution, 1.0f / tileResolution);
                                }
                                // Back to bakeDrapeTile's state; beginOffscreen runs once per frame.
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
                        // Occlusion masks (#175), same pass and fingerprint as the drape; stack 1+k, R8.
                        for (std::size_t k = 0; k < drapeCuts.size(); k++) {
                            std::size_t maskFingerprint = bakedFingerprint ^ (drapeCutSignature + k * 0x9e3779b9);
                            bool maskNeedsBake = false, maskHasContent = false;
                            unsigned int maskTexture = _terrainDrapeCache->acquire(request.tileId, static_cast<int>(k) + 1, maskFingerprint, tileResolution, nullptr, maskNeedsBake, maskHasContent);
                            if (maskTexture == 0) {
                                VT_STAT_INC(drapeMaskAcquireFail); // never markBaked: this tile re-bakes next frame too
                                continue;
                            }
                            bindDrapeTarget(maskTexture, _terrainDrapeCache->getTextureResolution(request.tileId, static_cast<int>(k) + 1));
                            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                            glClear(GL_COLOR_BUFFER_BIT);
                            for (std::size_t i = drapeCuts[k].layerIndex; i < drapeLayers.size(); i++) {
                                // The cut's own layer starts at the cut; later layers are wholly above it.
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
                    // Counts set urgency; time sets how many. Priority order until spent, but always one.
                    const cglib::mat4x4<double>& bakeMVPMatrix = viewState.getModelviewProjectionMat();
                    std::chrono::steady_clock::time_point bakeNow = std::chrono::steady_clock::now();
                    if (!(_drapeBakeLastMVPMatrix == bakeMVPMatrix)) {
                        _drapeBakeLastMoveTime = bakeNow;
                    }
                    // Plus a settle window: a fast zoom is a chain of gestures, and each rest must not get the at-rest budget.
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
                    VT_STAT_ADD(drapeQueuedBlank, static_cast<long long>(blankTiles.size()));
                    VT_STAT_ADD(drapeQueuedRestack, static_cast<long long>(restackTiles.size()));
                    VT_STAT_ADD(drapeQueuedStandIn, static_cast<long long>(standInTiles.size()));
                    VT_STAT_ADD(drapeQueuedPartial, static_cast<long long>(partialTiles.size()));
                    VT_STAT_ADD(drapeQueuedStale, static_cast<long long>(staleTiles.size()));
                    auto bakeTimeLeft = [&bakeStart, &bakedThisFrame, bakeTimeBudget]() {
                        if (bakedThisFrame == 0) {
                            return true; // always make progress
                        }
                        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bakeStart).count() < bakeTimeBudget;
                    };
                    // The only reason to ask for another frame; asking on queue size spins a still map forever.
                    bool drapeBakesLeft = false;
                    auto bakeSome = [&](std::vector<BakeRequest>& tiles, int budget) {
                        auto it = tiles.begin();
                        for (; it != tiles.end() && budget > 0 && bakeTimeLeft(); it++, budget--) {
                            bakeTile(*it);
                        }
                        drapeBakesLeft = drapeBakesLeft || it != tiles.end();
                    };
                    bakeSome(blankTiles, DRAPE_BAKE_BUDGET_BLANK);
                    bakeSome(restackTiles, DRAPE_BAKE_BUDGET_RESTACK);
                    // Deck drape per render tile (one draw, one texture), right after the blank ground.
                    if (!spanDrapeTiles.empty()) {
                        std::map<vt::TileId, unsigned int> spanDrapeTextures;
                        beginOffscreen();
                        // Budgeted, nearest the focus first; an undraped deck draws its plain roof.
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
                            if (spanNeedsBake && spanBakedThisFrame >= DRAPE_BAKE_BUDGET_SPAN && !bakeTimeLeft()) {
                                spanBakesLeft = true;
                                // An older road beats a bare deck, which flashes dark.
                                if (_terrainDrapeCache->isBaked(it->first, SPAN_DRAPE_STACK)) {
                                    spanDrapeTextures[it->first] = spanTexture;
                                }
                                continue;
                            }
                            if (spanNeedsBake) {
                                spanBakedThisFrame++;
                                bakedThisFrame++;
                                VT_STAT_INC(drapeBakes);
                                bindDrapeTarget(spanTexture, _terrainDrapeCache->getTextureResolution(it->first, SPAN_DRAPE_STACK));
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
                    bakeSome(standInTiles, DRAPE_BAKE_BUDGET_STANDIN);
                    bakeSome(partialTiles, DRAPE_BAKE_BUDGET_PARTIAL);
                    // Count-rationed while moving; at rest only the time budget, as the whole cover can go stale at once.
                    bakeSome(staleTiles, bakeCameraMoving ? DRAPE_BAKE_BUDGET_STALE : static_cast<int>(staleTiles.size()));

                    // Nothing else asks for the frames rationed baking needs.
                    if (drapeBakesLeft) {
                        requestRedraw();
                    }
                    // What the 2D/3D switch's settle report reads, one frame later.
                    _drapeBakesPending = drapeBakesLeft;
                    _drapeBakesDone += bakedThisFrame;
                    VT_STAT_ADD(drapeBakeNs, std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - bakeStart).count());
                    if (bakeStarted) {
                        // Detach before sampling: sampling a still-attached texture is undefined (black on the emulator).
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
                        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
                        glViewport(0, 0, viewState.getWidth(), viewState.getHeight());
                    }

                    // Handed over before the layers draw; an unbaked mask leaves its live layers unmasked meanwhile.
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

                    // GL_LEQUAL: the background already wrote the same meshes' depth.
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests
                    groundAODraped = groundAODraped || !drapedTiles.empty();
                    for (auto it = drapedTiles.begin(); it != drapedTiles.end(); it++) {
                        // Always drawn: the only depth writer, a skipped tile leaves a depth hole.
                        if (it->texture != 0) {
                            surfaceDraws += drapeLayers.front()->renderDrapedSurface(it->tileId, it->texture, it->uvOffsetX, it->uvOffsetY, it->uvScale);
                        } else {
                            surfaceDraws += drapeLayers.front()->renderDrapedSurfaceFill(it->tileId, drapeClearColor);
                            filledSurfaces++;
                        }
                    }
                    // A bridge deck's roof past its portals wears the ground's drape (polygon3DFsh).
                    {
                        std::map<vt::TileId, TileLayer::GroundDrapeRef> groundDrapes;
                        for (auto it = drapedTiles.begin(); it != drapedTiles.end(); it++) {
                            if (it->texture != 0) {
                                groundDrapes[it->tileId] = TileLayer::GroundDrapeRef { it->texture, it->uvOffsetX, it->uvOffsetY, it->uvScale };
                            }
                        }
                        for (std::size_t i = 0; i < drapeLayers.size(); i++) {
                            drapeLayers[i]->setGroundDrapeTextures(groundDrapes);
                        }
                    }
                    glEnable(GL_CULL_FACE);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_FALSE);
                    _terrainDrapeCache->endFrame();


                    // Logged in the frame: a brief flash never coincides with the periodic dump.
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
                        Log::Infof("MapRenderer: RTT drape cost avg %.1f ms, max %.1f ms over %d frames; queued blank %d stand-in %d partial %d stale %d, more left %d",
                                   drapeMsSum / std::max(1, drapeMsCount), drapeMsMax, drapeMsCount,
                                   static_cast<int>(blankTiles.size()), static_cast<int>(standInTiles.size()),
                                   static_cast<int>(partialTiles.size()), static_cast<int>(staleTiles.size()), drapeBakesLeft ? 1 : 0);
                        drapeMsSum = 0; drapeMsMax = 0; drapeMsCount = 0;
                    }
                    if ((drapeStateFrame % 600) == 1 && drapedTiles.size() > 0) {
                        int minZoom = 99, maxZoom = -1;
                        for (auto it2 = drapedTiles.begin(); it2 != drapedTiles.end(); it2++) {
                            minZoom = std::min(minZoom, it2->tileId.zoom);
                            maxZoom = std::max(maxZoom, it2->tileId.zoom);
                        }
                        Log::Infof("MapRenderer: RTT drape tiles zoom %d..%d, count %d", minZoom, maxZoom, static_cast<int>(drapedTiles.size()));
                        // A standing 'partial' backlog means the bake never catches up with the layers.
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
                        // Shader compile/link failures throw from the render thread.
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
                // Release a stale cover, or a layer keeps suppressing its own depth pre-pass.
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

        std::vector<std::shared_ptr<BillboardDrawData> > billboardDrawDatas;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            billboardDrawDatas.reserve(_billboardDrawDatas.size());
        }
        BillboardSorter billboardSorter(billboardDrawDatas);

        // A terrain map with no ground layer closes the prelude here.
        if (!preludeAccounted) {
            FRAME_PROF_ADD(preludeMs, profDrawStart);
        }

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

        // Undraped contact shadows: one screen mask under MIN, as overlapping quads would compound to black.
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
                // One multiply, before the extrusions draw over their own footprints.
                if (aoDraws > 0) {
                    multiplyScreenMask(_groundAOMaskBuffer->getTexture(), 1.0f / viewState.getWidth(), 1.0f / viewState.getHeight());
                }
                FRAME_PROF_GPU_END();
            }
        }

        FRAME_PROF_NOW(profLayer3DStart);
        FRAME_PROF_GPU_BEGIN(SECTION_LAYERS3D);
        for (std::size_t i = 0; i < layers.size(); i++) {
            if (layers[i]->onDrawFrame3D(deltaSeconds, billboardSorter, viewState)) {
                needRedraw = true;
                redrawMask |= 1u << (16 + std::min<std::size_t>(i, 15));
            }
        }

        FRAME_PROF_ADD(layer3DMs, profLayer3DStart);

        FRAME_PROF_NOW(profBillboardStart);
        FRAME_PROF_GPU_BEGIN(SECTION_BILLBOARDS);
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

        FRAME_PROF_ADD(billboardMs, profBillboardStart);
        FRAME_PROF_GPU_END();

        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _billboardDrawDatas = std::move(billboardDrawDatas);
        }
    
        if (needRedraw) {
            requestRedraw();
        }
        // Mask: low half base pass, high half 3D pass, a bit per layer; no bit = an external requestRedraw.
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

        // Joined to drawLayers' list: the placement worker reads one.
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

            if (optionName == "TileDrawSize" || optionName == "ZoomOffset" || optionName == "DPI" || optionName == "DrawDistance" || optionName == "FieldOfViewY" || optionName == "FocusPointOffset") {
                std::lock_guard<std::recursive_mutex> lock(mapRenderer->_mutex);
                mapRenderer->_viewState.calculateViewState(*mapRenderer->_options);
                updateView = true;
            }

            if (optionName.substr(0, 27) == "TerrainOptions.ViewDistance") {
                // The near and far planes are only recomputed when the camera changes, so a new view
                // distance (or its ceiling, or its factor) waited for the next look around to apply.
                std::lock_guard<std::recursive_mutex> lock(mapRenderer->_mutex);
                mapRenderer->_viewState.cameraChanged();
                mapRenderer->_viewState.calculateViewState(*mapRenderer->_options);
            }

            if (optionName.substr(0, 14) == "TerrainOptions") {
                // Tile layers rebuild with/without terrain displacement on a cull pass.
                updateView = true;
            }

            if (optionName.substr(0, 10) == "FogOptions") {
                // A redraw keeps the drape bake and shadow mask; a cull pass refreshes them.
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
    // A quarter zoom level is ~20% more room under the labels; the delay places once per gesture.
    const float MapRenderer::LABEL_PLACEMENT_ZOOM_THRESHOLD = 0.25f;
    const int MapRenderer::LABEL_PLACEMENT_ZOOM_DELAY = 250;

    const float MapRenderer::DRAPE_REBAKE_ZOOM_THRESHOLD = 0.25f;

    const int MapRenderer::ELEVATION_REFRESH_DELAY = 500;
    const float MapRenderer::EYE_GROUND_SETTLE_TIME = 0.3f;

    // Late 3D beats a map pinned flat by one tile that never loads.
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

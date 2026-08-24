/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FRAMEPROFILER_H_
#define _MASSIF_FRAMEPROFILER_H_

/**
 * Per-frame render timing, split by the section of the frame it was spent in.
 *
 * The sections are the ones a rendering change actually moves: the terrain occlusion pass,
 * the drape cover and its bakes, the layer draw, the 3D pass. Together with the frame rate
 * they say WHERE a frame went, which is the difference between optimising the renderer and
 * guessing at it.
 *
 * It lives in the hot path - one clock read per section per frame - so it must cost nothing
 * when unused: with MASSIF_FRAME_PROFILER at 0 the counters do not exist, the FRAME_PROF_*
 * macros expand to nothing, and their arguments are never evaluated.
 *
 * Enable it for a debug build with -DMASSIF_FRAME_PROFILER=1 (scripts/android-dev passes
 * CMake flags through to the native build), then read the 'PROF' lines from logcat.
 */
#ifndef MASSIF_FRAME_PROFILER
#define MASSIF_FRAME_PROFILER 0
#endif

#if MASSIF_FRAME_PROFILER

#include <algorithm>
#include <chrono>

#include <vt/RenderStats.h>

#include "utils/Log.h"

namespace massif {
    /**
     * The same frame sections, measured on the GPU with GL_EXT_disjoint_timer_query.
     *
     * A CPU timer inside a GL section measures where the driver decided to block, not where the
     * work is - that is why the 3D layer pass reads as 18 ms with only ~4 ms of it attributable.
     * A timer query is answered by the GPU itself, so it says how much of a section is actually
     * GPU work.
     *
     * Results are read SLOT_COUNT frames late, so the render thread never waits for the GPU; a
     * frame whose results are not back yet is skipped rather than measured half way. Sections
     * cannot nest (one TIME_ELAPSED query is active at a time), which matches the sections here:
     * they are sequential.
     *
     * CAVEAT when reading the numbers: TIME_ELAPSED covers the wall clock between the two markers
     * in the command stream, so a section where the GPU idles waiting for the CPU counts that
     * idle time. The trustworthy signal is the TOTAL against the frame time (GPU-bound or not)
     * and the RATIO between sections, not a single section in isolation.
     */
    struct GpuFrameProfiler {
        enum Section {
            SECTION_SKY = 0,
            SECTION_BACKGROUND,
            SECTION_PRELUDE,
            SECTION_PREPARE,
            SECTION_COVER,
            SECTION_DRAPE,
            SECTION_LAYERS,
            SECTION_LAYERS3D,
            SECTION_BILLBOARDS,
            SECTION_SHADOWCAST,  // the shadow map's caster pass
            SECTION_SHADOWMASK,  // the screen-space terrain shadow mask
            SECTION_GROUNDAO,    // the screen-space contact-shadow mask of the extrusions
        SECTION_LABELOCC,    // the occluder depth labels test their anchors against
            SECTION_COUNT
        };

        // Called at the start of every frame: collects whatever the GPU has finished and picks
        // the query slot this frame writes into.
        static void beginFrame();
        static void beginSection(int section);
        static void endSection();
        // Logs the averages over the interval the CPU profiler just reported, then resets them.
        static void logInterval();
    };

    struct FrameProfiler {
        // Where the current frame spent its time. Reset at the start of every frame.
        static inline double skyMs = 0;        // frame start: state, sky, background (includes the swap-buffer wait)
        static inline double preludeMs = 0;    // terrain depth pre-pass / occlusion depth read-back
        static inline double prepareMs = 0;    // per-layer startFrame (label re-anchoring, blending state)
        static inline double coverMs = 0;      // drape cover computation
        static inline double drapeMs = 0;      // drape bakes + terrain surface draws
        static inline double layerMs = 0;      // base layer draw pass
        static inline double layer3DMs = 0;    // 3D layer draw pass
        static inline double billboardMs = 0;  // billboard sorting and drawing

        static double now() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        static void resetFrame() {
            skyMs = preludeMs = prepareMs = coverMs = drapeMs = layerMs = layer3DMs = billboardMs = 0;
            GpuFrameProfiler::beginFrame();
        }

        /**
         * A single frame far above the average, reported on its own.
         *
         * The once-a-second average hides exactly the frame the user feels - a 108 ms frame
         * inside a 48 ms average is invisible in the mean and is the whole complaint. This
         * prints that frame's own section split next to the counters that moved DURING it,
         * which is what separates "the tile set changed and every label was rebuilt" from
         * "the draw itself was slow": the deltas are per-frame, so a spike with tileSets 0 and
         * labelMaps 0 is not a tile-set change however plausible that sounded.
         *
         * Called on EVERY frame, not only slow ones - the snapshot has to advance each frame or
         * the deltas would span from the previous spike instead of the previous frame.
         */
        static void checkSpike(double frameMs) {
            // The threshold is deliberately a fixed number rather than a multiple of the running
            // average: while zooming the average itself climbs, and a ratio stops firing exactly
            // when the stalls get bad.
            constexpr double SPIKE_MS = 80.0;
#if MASSIF_VT_RENDER_STATS
            using vt::RenderStats;
            struct Snapshot {
                long long tileSets, labelMaps, labelsAlloc, snaps, cullPasses, geomDraws, surfBuilt;
            };
            static Snapshot last = { 0, 0, 0, 0, 0, 0, 0 };
            Snapshot now = {
                RenderStats::visibleTileSetChanges.load(), RenderStats::labelMapRebuilds.load(),
                RenderStats::labelsAllocated.load(), RenderStats::snapPlacements.load(),
                RenderStats::cullerPasses.load(), RenderStats::geometryDraws.load(),
                RenderStats::tileSurfacesBuilt.load()
            };
            Snapshot delta = {
                now.tileSets - last.tileSets, now.labelMaps - last.labelMaps,
                now.labelsAlloc - last.labelsAlloc, now.snaps - last.snaps,
                now.cullPasses - last.cullPasses, now.geomDraws - last.geomDraws,
                now.surfBuilt - last.surfBuilt
            };
            last = now;
#endif
            if (frameMs < SPIKE_MS) {
                return;
            }
            double other = frameMs - skyMs - preludeMs - prepareMs - coverMs - drapeMs
                         - layerMs - layer3DMs - billboardMs;
#if MASSIF_VT_RENDER_STATS
            Log::Infof("PROF SPIKE: frame %.1f ms | sky %.1f prelude %.1f prepare %.1f cover %.1f "
                       "drape %.1f layers %.1f layers3D %.1f billboards %.1f other %.1f "
                       "| THIS FRAME: tileSets %lld labelMaps %lld labelsAlloc %lld snaps %lld "
                       "cullPasses %lld geomDraws %lld surfBuilt %lld",
                       frameMs, skyMs, preludeMs, prepareMs, coverMs, drapeMs, layerMs,
                       layer3DMs, billboardMs, other,
                       delta.tileSets, delta.labelMaps, delta.labelsAlloc, delta.snaps,
                       delta.cullPasses, delta.geomDraws, delta.surfBuilt);
#else
            Log::Infof("PROF SPIKE: frame %.1f ms | sky %.1f prelude %.1f prepare %.1f cover %.1f "
                       "drape %.1f layers %.1f layers3D %.1f billboards %.1f other %.1f "
                       "| (build with MASSIF_VT_RENDER_STATS=1 for the per-frame counters)",
                       frameMs, skyMs, preludeMs, prepareMs, coverMs, drapeMs, layerMs,
                       layer3DMs, billboardMs, other);
#endif
        }

        // Accumulates one frame and prints the running averages once a second. 'frameMs' is
        // the whole frame; whatever it does not account for is reported as 'other'.
        static void endFrame(double frameMs) {
            checkSpike(frameMs);

            static double sumMs = 0, maxMs = 0, sumSky = 0, sumPrelude = 0, sumPrepare = 0;
            static double sumCover = 0, sumDrape = 0, sumLayer = 0, sumLayer3D = 0, sumBillboard = 0;
            static int count = 0;
            static std::chrono::steady_clock::time_point lastLog = std::chrono::steady_clock::now();

            sumMs += frameMs;
            maxMs = std::max(maxMs, frameMs);
            sumSky += skyMs; sumPrelude += preludeMs; sumPrepare += prepareMs; sumCover += coverMs;
            sumDrape += drapeMs; sumLayer += layerMs; sumLayer3D += layer3DMs; sumBillboard += billboardMs;
            count++;

            std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
            if (currentTime - lastLog < std::chrono::milliseconds(1000)) {
                return;
            }
            double intervalMs = std::chrono::duration<double, std::milli>(currentTime - lastLog).count();
            Log::Infof("PROF: %d frames in %.0f ms (%.1f fps), frame avg %.1f max %.1f | sky %.1f prelude %.1f prepare %.1f cover %.1f drape %.1f layers %.1f layers3D %.1f billboards %.1f other %.1f",
                count, intervalMs, count * 1000.0 / intervalMs, sumMs / count, maxMs,
                sumSky / count, sumPrelude / count, sumPrepare / count, sumCover / count,
                sumDrape / count, sumLayer / count, sumLayer3D / count, sumBillboard / count,
                (sumMs - sumSky - sumPrelude - sumPrepare - sumCover - sumDrape - sumLayer - sumLayer3D - sumBillboard) / count);
            GpuFrameProfiler::logInterval();
            sumMs = maxMs = sumSky = sumPrelude = sumPrepare = sumCover = sumDrape = sumLayer = sumLayer3D = sumBillboard = 0;
            count = 0;
            lastLog = currentTime;
        }
    };
}

#define FRAME_PROF_RESET() (massif::FrameProfiler::resetFrame())
#define FRAME_PROF_NOW(var) double var = massif::FrameProfiler::now()
#define FRAME_PROF_ADD(field, startVar) (massif::FrameProfiler::field += massif::FrameProfiler::now() - (startVar))
#define FRAME_PROF_SET(field, value) (massif::FrameProfiler::field = (value))
#define FRAME_PROF_END(startVar) (massif::FrameProfiler::endFrame(massif::FrameProfiler::now() - (startVar)))
#define FRAME_PROF_GPU_BEGIN(section) (massif::GpuFrameProfiler::beginSection(massif::GpuFrameProfiler::section))
#define FRAME_PROF_GPU_END() (massif::GpuFrameProfiler::endSection())

#else

#define FRAME_PROF_RESET() ((void)0)
#define FRAME_PROF_NOW(var) ((void)0)
#define FRAME_PROF_ADD(field, startVar) ((void)0)
#define FRAME_PROF_SET(field, value) ((void)0)
#define FRAME_PROF_END(startVar) ((void)0)
#define FRAME_PROF_GPU_BEGIN(section) ((void)0)
#define FRAME_PROF_GPU_END() ((void)0)

#endif

#endif

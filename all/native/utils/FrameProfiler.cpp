#include "utils/FrameProfiler.h"

#if MASSIF_FRAME_PROFILER

#include "utils/Log.h"

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#include <algorithm>
#include <cstring>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace massif {

#ifdef GL_EXT_disjoint_timer_query

    namespace {
        // How many frames of queries are in flight. The GPU is typically 1-2 frames behind the
        // command stream, so 4 slots keep a result ready every frame without ever waiting.
        const int SLOT_COUNT = 4;

        struct QuerySlot {
            GLuint queries[GpuFrameProfiler::SECTION_COUNT];
            bool used[GpuFrameProfiler::SECTION_COUNT];
            bool pending;
        };

        // A section longer than this did not happen: the Adreno driver answers a query it could not
        // time with 0xFFFFFFFF while still reporting it available. Dropped ON ITS OWN - dropping the
        // whole frame threw away every frame in which one section landed on a flush.
        const double MAX_PLAUSIBLE_MS = 500.0;

        PFNGLGENQUERIESEXTPROC GenQueriesEXT = NULL;
        PFNGLBEGINQUERYEXTPROC BeginQueryEXT = NULL;
        PFNGLENDQUERYEXTPROC EndQueryEXT = NULL;
        PFNGLGETQUERYOBJECTUIVEXTPROC GetQueryObjectuivEXT = NULL;

        QuerySlot Slots[SLOT_COUNT];
        int CurrentSlot = -1;
        int ActiveSection = -1;
        bool Initialized = false;
        bool Supported = false;

        double SumMs[GpuFrameProfiler::SECTION_COUNT];
        int SectionFrames[GpuFrameProfiler::SECTION_COUNT];
        int SectionDrops[GpuFrameProfiler::SECTION_COUNT];
        int MeasuredFrames = 0;
        int DisjointFrames = 0;

        void Initialize() {
            Initialized = true;

#ifdef __ANDROID__
            // The queries themselves cost frame time on a tiler, so the profiled build must be
            // able to run without them: 'adb shell setprop debug.massif.gputimer 0'.
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.gputimer", property) > 0 && property[0] == '0') {
                Log::Info("GpuFrameProfiler: disabled by debug.massif.gputimer");
                return;
            }
#endif

            const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            if (!extensions || !std::strstr(extensions, "GL_EXT_disjoint_timer_query")) {
                Log::Info("GpuFrameProfiler: GL_EXT_disjoint_timer_query not supported, GPU timings disabled");
                return;
            }

            GenQueriesEXT = reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));
            BeginQueryEXT = reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));
            EndQueryEXT = reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));
            GetQueryObjectuivEXT = reinterpret_cast<PFNGLGETQUERYOBJECTUIVEXTPROC>(eglGetProcAddress("glGetQueryObjectuivEXT"));
            if (!GenQueriesEXT || !BeginQueryEXT || !EndQueryEXT || !GetQueryObjectuivEXT) {
                Log::Info("GpuFrameProfiler: timer query entry points missing, GPU timings disabled");
                return;
            }

            for (int i = 0; i < SLOT_COUNT; i++) {
                GenQueriesEXT(GpuFrameProfiler::SECTION_COUNT, Slots[i].queries);
                std::fill(Slots[i].used, Slots[i].used + GpuFrameProfiler::SECTION_COUNT, false);
                Slots[i].pending = false;
            }
            std::fill(SumMs, SumMs + GpuFrameProfiler::SECTION_COUNT, 0.0);
            std::fill(SectionFrames, SectionFrames + GpuFrameProfiler::SECTION_COUNT, 0);
            std::fill(SectionDrops, SectionDrops + GpuFrameProfiler::SECTION_COUNT, 0);

            Supported = true;
            Log::Info("GpuFrameProfiler: GPU timings enabled, read the 'PROF GPU' lines");
        }

        // Reads one slot back, but only once EVERY query in it has an answer - a half-read slot
        // would have to be re-read later and its sections counted twice.
        void HarvestSlot(QuerySlot& slot) {
            if (!slot.pending) {
                return;
            }
            for (int i = 0; i < GpuFrameProfiler::SECTION_COUNT; i++) {
                if (!slot.used[i]) {
                    continue;
                }
                GLuint available = 0;
                GetQueryObjectuivEXT(slot.queries[i], GL_QUERY_RESULT_AVAILABLE_EXT, &available);
                if (!available) {
                    return;
                }
            }
            // The 32-bit result, not the 64-bit one: glGetQueryObjectui64vEXT comes back
            // unwritten on this Adreno driver. 32 bits of nanoseconds is 4.3 s, far past any
            // section worth measuring.
            for (int i = 0; i < GpuFrameProfiler::SECTION_COUNT; i++) {
                if (!slot.used[i]) {
                    continue;
                }
                GLuint elapsedNs = 0;
                GetQueryObjectuivEXT(slot.queries[i], GL_QUERY_RESULT_EXT, &elapsedNs);
                double elapsedMs = elapsedNs / 1000000.0;
                if (elapsedMs > MAX_PLAUSIBLE_MS) {
                    SectionDrops[i]++;
                    continue;
                }
                SumMs[i] += elapsedMs;
                SectionFrames[i]++;
            }
            MeasuredFrames++;
            slot.pending = false;
        }
    }

    void GpuFrameProfiler::beginFrame() {
        if (!Initialized) {
            Initialize();
        }
        if (!Supported) {
            return;
        }

        // A section left open by an early return out of the frame would make the next
        // glBeginQueryEXT fail, so close it before anything else.
        endSection();

        // The disjoint flag covers everything since it was last read - a clock change or a
        // context switch on the GPU makes every result in flight meaningless, not just one.
        GLint disjoint = 0;
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
        if (disjoint) {
            for (int i = 0; i < SLOT_COUNT; i++) {
                if (Slots[i].pending) {
                    Slots[i].pending = false;
                    DisjointFrames++;
                }
            }
        } else {
            for (int i = 0; i < SLOT_COUNT; i++) {
                HarvestSlot(Slots[i]);
            }
        }

        CurrentSlot = -1;
        for (int i = 0; i < SLOT_COUNT; i++) {
            if (!Slots[i].pending) {
                CurrentSlot = i;
                std::fill(Slots[i].used, Slots[i].used + SECTION_COUNT, false);
                break;
            }
        }
    }

    void GpuFrameProfiler::beginSection(int section) {
        if (!Supported || CurrentSlot < 0 || section < 0 || section >= SECTION_COUNT) {
            return;
        }
        endSection();

        QuerySlot& slot = Slots[CurrentSlot];
        BeginQueryEXT(GL_TIME_ELAPSED_EXT, slot.queries[section]);
        slot.used[section] = true;
        slot.pending = true;
        ActiveSection = section;
    }

    void GpuFrameProfiler::endSection() {
        if (!Supported || ActiveSection < 0) {
            return;
        }
        EndQueryEXT(GL_TIME_ELAPSED_EXT);
        ActiveSection = -1;
    }

    void GpuFrameProfiler::logInterval() {
        if (!Supported) {
            return;
        }
        if (MeasuredFrames == 0) {
            if (DisjointFrames > 0) {
                Log::Infof("PROF GPU: no frame read back, %d dropped (disjoint)", DisjointFrames);
                DisjointFrames = 0;
            }
            return;
        }

        // Each section carries its OWN frame count: a section the driver could not time is
        // missing from that frame only, so dividing everything by the frame count would report
        // it as cheap rather than as unmeasured. 'total' is the sum of the section averages.
        double avgMs[SECTION_COUNT] = { 0 };
        double totalMs = 0;
        int totalDrops = 0;
        for (int i = 0; i < SECTION_COUNT; i++) {
            avgMs[i] = SectionFrames[i] > 0 ? SumMs[i] / SectionFrames[i] : 0.0;
            totalMs += avgMs[i];
            totalDrops += SectionDrops[i];
        }
        Log::Infof("PROF GPU: %d frames, %d dropped (disjoint), %d sections untimed | sky %.1f background %.1f prelude %.1f prepare %.1f cover %.1f drape %.1f layers %.1f layers3D %.1f billboards %.1f shadowCast %.1f shadowMask %.1f groundAO %.1f labelOcc %.1f total %.1f",
            MeasuredFrames, DisjointFrames, totalDrops,
            avgMs[SECTION_SKY], avgMs[SECTION_BACKGROUND], avgMs[SECTION_PRELUDE], avgMs[SECTION_PREPARE], avgMs[SECTION_COVER],
            avgMs[SECTION_DRAPE], avgMs[SECTION_LAYERS], avgMs[SECTION_LAYERS3D], avgMs[SECTION_BILLBOARDS], avgMs[SECTION_SHADOWCAST], avgMs[SECTION_SHADOWMASK], avgMs[SECTION_GROUNDAO], avgMs[SECTION_LABELOCC],
            totalMs);

        std::fill(SumMs, SumMs + SECTION_COUNT, 0.0);
        std::fill(SectionFrames, SectionFrames + SECTION_COUNT, 0);
        std::fill(SectionDrops, SectionDrops + SECTION_COUNT, 0);
        MeasuredFrames = 0;
        DisjointFrames = 0;
    }

#else

    void GpuFrameProfiler::beginFrame() {
        static bool warned = false;
        if (!warned) {
            warned = true;
            Log::Info("GpuFrameProfiler: built without GL_EXT_disjoint_timer_query headers, GPU timings disabled");
        }
    }

    void GpuFrameProfiler::beginSection(int section) {
    }

    void GpuFrameProfiler::endSection() {
    }

    void GpuFrameProfiler::logInterval() {
    }

#endif

}

#endif

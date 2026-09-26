/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VTLABELPLACEMENTWORKER_H_
#define _MASSIF_VTLABELPLACEMENTWORKER_H_

#include "components/ThreadWorker.h"
#include "graphics/ViewState.h"

#include <set>

#include <vt/LabelCuller.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace massif {
    class Layer;
    class MapRenderer;
    
    class VTLabelPlacementWorker : public ThreadWorker {
    public:
        VTLabelPlacementWorker();
        virtual ~VTLabelPlacementWorker();
        
        void setComponents(const std::weak_ptr<MapRenderer>& mapRenderer, const std::shared_ptr<VTLabelPlacementWorker>& worker);
        
        void init(const std::shared_ptr<Layer>& layer, int delayTime);
        /** Same, but pushes an already pending pass back instead of running it earlier. */
        void postpone(const std::shared_ptr<Layer>& layer, int delayTime);
        
        void stop();
        
        bool isIdle() const;
    
        void operator()();
    
    private:
        void run();
        void schedule(const std::shared_ptr<Layer>& layer, int delayTime, bool postpone);
        
        bool calculateVTLabelPlacement();
        void scheduleContinuation();

        /**
         * A cycle may span passes (mapbox's PauseablePlacement; off while PLACEMENT_BUDGET_MS is 0), so culler,
         * grid and the frozen view outlive a pass.
         */
        std::unique_ptr<vt::LabelCuller> _culler;
        ViewState _cycleViewState;
        bool _cycleActive = false;
        /** A cycle ends when every layer has wrapped once; under slicing they never wrap in the same pass. */
        std::set<const void*> _cycleWrappedLayers;
        /** Commit the next placement unfaded (TileRenderer::snapLabelTransition); only after an abandoned cycle. */
        bool _snapNextPlacement = false;
        double _cycleMs = 0;
        /** No pass may start before this: the CPU duty cycle. */
        std::chrono::steady_clock::time_point _nextAllowedTime;
        
        bool _stop;
        bool _idle;
        
        bool _pendingWakeup;
        std::chrono::steady_clock::time_point _wakeupTime;
        // Start of the last pass, to hold the next one off until its fade finished.
        std::chrono::steady_clock::time_point _lastPassTime;
        /**
         * Shortest gap between placement passes, ms: maplibre's Placement.stillRecent / fadeDuration, and the
         * default label fade. Without it every arriving tile re-placed and labels never finished fading.
         */
        static const int MIN_PLACEMENT_INTERVAL;

        std::weak_ptr<MapRenderer> _mapRenderer;
        std::shared_ptr<VTLabelPlacementWorker> _worker;
    
        std::condition_variable _condition;
        mutable std::mutex _mutex;
    };
    
}

#endif

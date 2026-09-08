/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VTLABELPLACEMENTWORKER_H_
#define _MASSIF_VTLABELPLACEMENTWORKER_H_

#include "components/ThreadWorker.h"
#include "graphics/ViewState.h"

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
         * A placement cycle is rationed across several passes (mapbox's PauseablePlacement), so the
         * culler, its collision grid and the view it was opened against all outlive one pass. The
         * view is FROZEN for the cycle: resuming against a moved camera would collide the second
         * half of the labels against a grid built for a different screen.
         */
        std::unique_ptr<vt::LabelCuller> _culler;
        ViewState _cycleViewState;
        bool _cycleActive = false;
        /**
         * Wall clock the current cycle has spent, and what the last COMPLETED one cost. Slicing is
         * not free - each slice re-sorts and re-inserts its own subset, and the pacing stretches a
         * cycle over many passes - so a cycle that fits in one pass is run in one pass.
         */
        double _cycleMs = 0;
        double _lastCycleMs = 0;
        /** No pass may start before this: what holds placement to its share of wall clock. */
        std::chrono::steady_clock::time_point _nextAllowedTime;
        
        bool _stop;
        bool _idle;
        
        bool _pendingWakeup;
        std::chrono::steady_clock::time_point _wakeupTime;
        
        std::weak_ptr<MapRenderer> _mapRenderer;
        std::shared_ptr<VTLabelPlacementWorker> _worker;
    
        std::condition_variable _condition;
        mutable std::mutex _mutex;
    };
    
}

#endif

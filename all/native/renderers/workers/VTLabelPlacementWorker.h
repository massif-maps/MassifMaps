/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VTLABELPLACEMENTWORKER_H_
#define _MASSIF_VTLABELPLACEMENTWORKER_H_

#include "components/ThreadWorker.h"

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
        
        bool _stop;
        bool _idle;
        
        bool _pendingWakeup;
        std::chrono::steady_clock::time_point _wakeupTime;
        // When the last pass STARTED, so the next one can be held off until its fade has finished.
        std::chrono::steady_clock::time_point _lastPassTime;
        /**
         * Shortest gap between two placement passes - maplibre's Placement.stillRecent, whose own
         * interval IS its fadeDuration: it will not start a new placement while the previous one is
         * still fading. Ours are asked for by every tile that arrives, so a pan at high tilt ran
         * several a second and no label ever finished its fade; measured on the Grenoble preview,
         * 10-14 labels flipped on and off per pass with the flips landing mid-screen.
         *
         * 300 ms is the fade at the default label blending speed (TileRenderer), and maplibre's
         * own fadeDuration.
         */
        static const int MIN_PLACEMENT_INTERVAL;

        std::weak_ptr<MapRenderer> _mapRenderer;
        std::shared_ptr<VTLabelPlacementWorker> _worker;
    
        std::condition_variable _condition;
        mutable std::mutex _mutex;
    };
    
}

#endif

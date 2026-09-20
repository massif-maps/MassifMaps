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
         * A placement cycle MAY be rationed across several passes (mapbox's PauseablePlacement), so
         * the culler, its collision grid and the view it was opened against all outlive one pass.
         * The view is FROZEN for the cycle: resuming against a moved camera would collide the second
         * half of the labels against a grid built for a different screen.
         *
         * The ration is currently 0 - see PLACEMENT_BUDGET_MS - so a cycle is one pass, and this is
         * the mechanism for turning it back on rather than a thing the worker relies on.
         */
        std::unique_ptr<vt::LabelCuller> _culler;
        ViewState _cycleViewState;
        bool _cycleActive = false;
        /**
         * Which layers have already wrapped during the current cycle. A cycle ends when every layer
         * has wrapped at least ONCE, not when they all wrap in the same pass - under slicing they
         * never do, and the grid below then never clears.
         */
        std::set<const void*> _cycleWrappedLayers;
        /**
         * The next placement is COMMITTED rather than faded in (TileRenderer::snapLabelTransition).
         * Set only when a cycle was abandoned because the camera moved under it: there is no
         * outgoing screen left to cross-fade from, and fading would draw every outgoing label over
         * its replacement for the length of the fade. A redo owed to a camera that moved while the
         * pass ran is NOT this case - that is the ordinary state of a view being turned, and
         * snapping it made every name change read as a blink.
         */
        bool _snapNextPlacement = false;
        /** Wall clock the current cycle has spent. */
        double _cycleMs = 0;
        /** No pass may start before this: what holds placement to its share of wall clock. */
        std::chrono::steady_clock::time_point _nextAllowedTime;
        
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

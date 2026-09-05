/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CULLWORKER_H_
#define _MASSIF_CULLWORKER_H_

#include "components/ThreadWorker.h"
#include "core/MapEnvelope.h"
#include "renderers/components/CullState.h"

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace massif {
    class EnvelopeLayer;
    class Layer;
    class MapRenderer;
    class Options;
    
    class CullWorker : public ThreadWorker {
    public:
        CullWorker();
        virtual ~CullWorker();
        
        void setComponents(const std::weak_ptr<MapRenderer>& mapRenderer, const std::shared_ptr<CullWorker>& worker);
    
        void init(const std::shared_ptr<Layer>& layer, int delayTime);
        
        void stop();
        
        bool isIdle() const;
    
        void operator()();
    
    private:
        void run();
    
        void calculateCullState();
        void calculateEnvelope();
        void updateLayers(const std::vector<std::shared_ptr<Layer> >& layers);
    
        static const int MAX_VIEWPORT_TESSELATION_LEVEL;

        static const int MAX_ENVELOPE_POINTS;

        static const float VIEWPORT_SCALE;

        static const int SURFACE_WAIT_RETRY_DELAY;

        std::map<std::shared_ptr<Layer>, std::chrono::steady_clock::time_point> _layerWakeupMap;
        
        bool _firstCull;
        
        MapEnvelope _envelope;
        
        ViewState _viewState;

        // How far from the camera the map is drawn, in internal units; 0 = as far as the camera
        // can see. At a low tilt that is the ground to the horizon, which is hundreds of tiles.
        double _maxVisibleDistance;
    
        std::weak_ptr<MapRenderer> _mapRenderer;
        std::shared_ptr<CullWorker> _worker;
    
        bool _stop;
        bool _idle;
        std::condition_variable _condition;
        mutable std::mutex _mutex;
    };
    
}

#endif

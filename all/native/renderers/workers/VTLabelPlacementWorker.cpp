#include "VTLabelPlacementWorker.h"
#include "components/Layers.h"
#include "layers/VectorTileLayer.h"
#include "renderers/MapRenderer.h"
#include "renderers/TileRenderer.h"
#include "utils/Const.h"
#include "utils/Log.h"
#include "utils/ThreadUtils.h"

#include <vt/LabelCuller.h>

#include <cmath>

namespace massif {

    VTLabelPlacementWorker::VTLabelPlacementWorker() :
        _stop(false),
        _idle(false),
        _pendingWakeup(false),
        _wakeupTime(std::chrono::steady_clock::now() + std::chrono::hours(24)),
        _mapRenderer(),
        _condition(),
        _mutex()
    {
    }
    
    VTLabelPlacementWorker::~VTLabelPlacementWorker() {
    }
        
    void VTLabelPlacementWorker::setComponents(const std::weak_ptr<MapRenderer>& mapRenderer, const std::shared_ptr<VTLabelPlacementWorker>& worker) {
        _mapRenderer = mapRenderer;
        // When the map component gets destroyed all threads get detatched. Detatched threads need their worker objects to be alive,
        // so worker objects need to keep references to themselves, until the loop finishes.
        _worker = worker;
    }
        
    void VTLabelPlacementWorker::init(const std::shared_ptr<Layer>& layer, int delayTime) {
        schedule(layer, delayTime, false);
    }

    void VTLabelPlacementWorker::postpone(const std::shared_ptr<Layer>& layer, int delayTime) {
        schedule(layer, delayTime, true);
    }

    // 'postpone' pushes the pass back on every call instead of keeping the earliest deadline, so a
    // stream of triggers (a camera zooming) results in ONE pass, once it stops.
    void VTLabelPlacementWorker::schedule(const std::shared_ptr<Layer>& layer, int delayTime, bool postpone) {
        if (!std::dynamic_pointer_cast<VectorTileLayer>(layer)) {
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        std::chrono::steady_clock::time_point wakeupTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayTime);
        _idle = false;
        if (postpone) {
            _wakeupTime = (_pendingWakeup ? std::max(_wakeupTime, wakeupTime) : wakeupTime);
        }
        else {
            _wakeupTime = std::min(_wakeupTime, wakeupTime);
        }
        _pendingWakeup = true;
        _condition.notify_one();
    }
    
    void VTLabelPlacementWorker::stop() {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
        _condition.notify_all();
    }
    
    bool VTLabelPlacementWorker::isIdle() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _idle;
    }
        
    void VTLabelPlacementWorker::operator ()() {
        run();
        _worker.reset();
    }
    
    void VTLabelPlacementWorker::run() {
        ThreadUtils::SetThreadPriority(ThreadPriority::LOW);
    
        while (true) {
            bool run = false;
            {
                std::unique_lock<std::mutex> lock(_mutex);

                if (_stop) {
                    return;
                }

                std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
                if (_wakeupTime - currentTime < std::chrono::milliseconds(1)) {
                    run = true;
                    _pendingWakeup = false;
                    _wakeupTime = currentTime + std::chrono::hours(24);
                }

                if (!run) {
                    _idle = !_pendingWakeup;
                    _condition.wait_for(lock, _wakeupTime - currentTime);
                    _idle = false;
                }
            }

            if (run) {
                calculateVTLabelPlacement();
            }
        }
    }
    
    bool VTLabelPlacementWorker::calculateVTLabelPlacement() {
        std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock();
        if (!mapRenderer) {
            return false;
        }

        ViewState viewState = mapRenderer->getViewState();
        std::vector<std::shared_ptr<Layer>> layers = mapRenderer->getLayers()->getAll();

        // A composite layer draws its style-layer groups and its vector slots through internal
        // child layers that are not in the layer list - they append themselves here, in draw order.
        std::vector<std::shared_ptr<VectorTileLayer> > labelLayers;
        for (const std::shared_ptr<Layer>& layer : layers) {
            layer->collectLabelLayers(labelLayers);
        }

        vt::LabelCuller culler(Const::WORLD_SIZE);
        // Internal units per metre at the view's own latitude, so a label style's max-distance in
        // metres compares against world-space distances. Mercator stretches by 1/cos(latitude),
        // which at 45 degrees is a factor of 1.4.
        {
            double latitude = viewState.getFocusPos()(1) * Const::PI * 2.0 / Const::WORLD_SIZE;
            double coshLatitude = std::cosh(latitude);
            culler.setMetersToInternal(Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE * coshLatitude);
        }

        bool reversedOrder = mapRenderer->getOptions()->isLayersLabelsProcessedInReverseOrder();
        bool changed = false;
        if (reversedOrder) {
            for (auto it = labelLayers.rbegin(); it != labelLayers.rend(); it++) {
                if ((*it)->_tileRenderer->cullLabels(culler, viewState)) {
                    changed = true;
                }
            }
        } else {
            for (auto it = labelLayers.begin(); it != labelLayers.end(); it++) {
                if ((*it)->_tileRenderer->cullLabels(culler, viewState)) {
                    changed = true;
                }
            }
        }
        

        if (changed) {
            mapRenderer->requestRedraw();
        }

        return true;
    }

}

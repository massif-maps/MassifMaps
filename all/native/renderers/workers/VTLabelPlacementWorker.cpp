#include "VTLabelPlacementWorker.h"
#include "components/Layers.h"
#include "layers/VectorTileLayer.h"
#include "renderers/MapRenderer.h"
#include "renderers/TileRenderer.h"
#include "utils/Const.h"
#include "utils/Log.h"
#include "utils/ThreadUtils.h"

#include <vt/LabelCuller.h>

#include <algorithm>
#include <cmath>

namespace massif {

    VTLabelPlacementWorker::VTLabelPlacementWorker() :
        _stop(false),
        _idle(false),
        _pendingWakeup(false),
        _wakeupTime(std::chrono::steady_clock::now() + std::chrono::hours(24)),
        _lastPassTime(std::chrono::steady_clock::now() - std::chrono::hours(24)),
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
        // Never before the last pass has finished fading - see MIN_PLACEMENT_INTERVAL.
        wakeupTime = std::max(wakeupTime, _lastPassTime + std::chrono::milliseconds(MIN_PLACEMENT_INTERVAL));
        _idle = false;
        if (postpone) {
            _wakeupTime = (_pendingWakeup ? std::max(_wakeupTime, wakeupTime) : wakeupTime);
        }
        else {
            _wakeupTime = std::min(_wakeupTime, wakeupTime);
        }
        _wakeupTime = std::max(_wakeupTime, _nextAllowedTime);
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
                    // Stamped when the pass STARTS, under the same lock that claims it. Stamped on
                    // the way out instead, a schedule() arriving while the pass ran read the
                    // PREVIOUS pass's time, found the interval already spent and fired again as
                    // soon as this one finished - measured at 202 ms between passes.
                    _lastPassTime = currentTime;
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
    
    // maplibre will not BEGIN a placement before commitTime + fadeDuration (Placement.stillRecent),
    // so a label never re-places while the previous one is still fading. This is that gate; the
    // duty cycle below is the separate, cost-based one. TileRenderer's default label blending speed
    // is the same 300 ms.
    const int VTLabelPlacementWorker::MIN_PLACEMENT_INTERVAL = 300;

    // The only cap on placement's CPU share, since a cycle's size cannot bound its rate: after C ms
    // the next pass waits C * (1000/TARGET - 1), holding placement to TARGET ms of every second.
    // A cap, not a quota: a still map spends none.
    static const double PLACEMENT_TARGET_MS_PER_SECOND = 90.0;

    // Slicing is off, see culler.beginSlice below; the machinery still works and a non-zero budget restores it.
    static const double PLACEMENT_BUDGET_MS = 0.0;

    bool VTLabelPlacementWorker::calculateVTLabelPlacement() {
        std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock();
        if (!mapRenderer) {
            return false;
        }

        std::vector<std::shared_ptr<Layer>> layers = mapRenderer->getLayers()->getAll();

        // A composite layer draws its style-layer groups and its vector slots through internal
        // child layers that are not in the layer list - they append themselves here, in draw order.
        std::vector<std::shared_ptr<VectorTileLayer> > labelLayers;
        for (const std::shared_ptr<Layer>& layer : layers) {
            layer->collectLabelLayers(labelLayers);
        }

        // A cycle keeps the view it opened against (as mapbox freezes the transform): its grid is half
        // built for that screen. Once the camera has moved the rest is for a screen that is gone, so
        // the cycle is abandoned and restarted rather than drained.
        if (_cycleActive && mapRenderer->getViewState().getModelviewProjectionMat() != _cycleViewState.getModelviewProjectionMat()) {
            for (const std::shared_ptr<VectorTileLayer>& layer : labelLayers) {
                layer->_tileRenderer->restartLabelPlacement();
            }
            _cycleWrappedLayers.clear();
            _cycleMs = 0;
            _cycleActive = false;
            // Nothing left to fade from: commit the replacement outright. The only case that snaps.
            _snapNextPlacement = true;
        }
        if (!_cycleActive) {
            _cycleViewState = mapRenderer->getViewState();
        }
        const ViewState& viewState = _cycleViewState;

        // The culler outlives a pass: it carries the cycle's collision grid, and clearing it
        // mid-cycle would let the second half of the labels reuse slots the first half took.
        if (!_culler) {
            _culler = std::make_unique<vt::LabelCuller>(Const::WORLD_SIZE);
        }
        vt::LabelCuller& culler = *_culler;
        if (!_cycleActive) {
            culler.reset();
        }
        // Internal units per metre at the view's own latitude, so a label style's max-distance in
        // metres compares against world-space distances. Mercator stretches by 1/cos(latitude),
        // which at 45 degrees is a factor of 1.4.
        {
            double latitude = viewState.getFocusPos()(1) * Const::PI * 2.0 / Const::WORLD_SIZE;
            double coshLatitude = std::cosh(latitude);
            culler.setMetersToInternal(Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE * coshLatitude);
        }
        // In multiples of the camera-to-focus distance; a view along the ground needs it raised or off,
        // its focus being a few km ahead of a low camera (Options::setLabelViewDistance).
        culler.setLabelViewDistance(mapRenderer->getOptions()->getLabelViewDistance());
        // Not sliced: the duty cycle already caps the rate, and a slice sorts only its own subset, so an
        // early label claims a slot before a higher-priority one (a panorama's far summits). The price
        // is latency: a long cycle lands in one pass, and placement follows a turn in steps.
        bool forced = _snapNextPlacement;
        _snapNextPlacement = false;
        culler.beginSlice(PLACEMENT_BUDGET_MS);
        std::chrono::steady_clock::time_point passStart = std::chrono::steady_clock::now();

        bool reversedOrder = mapRenderer->getOptions()->isLayersLabelsProcessedInReverseOrder();
        bool changed = false;
        // Under slicing each layer wraps on a different pass, so the cycle ends once every layer has
        // wrapped at least once, or the grid is never cleared.
        auto cullLayer = [&](const std::shared_ptr<VectorTileLayer>& layer) {
            bool wrapped = true;
            if (layer->_tileRenderer->cullLabels(culler, viewState, wrapped)) {
                changed = true;
            }
            if (wrapped) {
                _cycleWrappedLayers.insert(layer->_tileRenderer.get());
            }
        };
        if (reversedOrder) {
            for (auto it = labelLayers.rbegin(); it != labelLayers.rend(); it++) {
                cullLayer(*it);
            }
        } else {
            for (auto it = labelLayers.begin(); it != labelLayers.end(); it++) {
                cullLayer(*it);
            }
        }
        bool finished = true;
        for (const std::shared_ptr<VectorTileLayer>& layer : labelLayers) {
            finished = _cycleWrappedLayers.count(layer->_tileRenderer.get()) > 0 && finished;
        }
        if (finished) {
            _cycleWrappedLayers.clear();
        }

        if (changed) {
            // Only an abandoned cycle snaps: a turning view's redo cross-fades, or a changed name blinks.
            if (forced) {
                for (const std::shared_ptr<VectorTileLayer>& layer : labelLayers) {
                    layer->_tileRenderer->snapLabelTransition();
                }
            }
            mapRenderer->requestRedraw();
        }

        double passMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - passStart).count();
        _cycleMs += passMs;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            double gapMs = passMs * (1000.0 / PLACEMENT_TARGET_MS_PER_SECOND - 1.0);
            _nextAllowedTime = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::milli>(gapMs));
        }

        // Only the cycle asks for the pass that continues it - nothing else knows one is owed, and
        // a still map stops waking this thread once the labels have settled.
        _cycleActive = !finished;
        if (_cycleActive) {
            scheduleContinuation();
        } else {
            _cycleMs = 0;
            // Placed for a camera that has since moved, and a still map stops waking this thread, so
            // the cycle asks for its own redo. No snap: the cross-fade keeps the change legible.
            if (mapRenderer->getViewState().getModelviewProjectionMat() != _cycleViewState.getModelviewProjectionMat()) {
                scheduleContinuation();
            }
        }

        return true;
    }

    void VTLabelPlacementWorker::scheduleContinuation() {
        std::lock_guard<std::mutex> lock(_mutex);

        _pendingWakeup = true;
        _wakeupTime = std::max(_nextAllowedTime, std::chrono::steady_clock::now());
        _idle = false;
        _condition.notify_one();
    }

}

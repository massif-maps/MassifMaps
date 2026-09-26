#include "VTLabelPlacementWorker.h"
#include "components/Layers.h"
#include "layers/VectorTileLayer.h"
#include "renderers/MapRenderer.h"
#include "renderers/TileRenderer.h"
#include "utils/Const.h"
#include "utils/Log.h"
#include "utils/ThreadUtils.h"

#include <vt/LabelCuller.h>
#include <vt/LabelFade.h>

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
        // Threads are detached on map destruction, so the worker keeps itself alive until its loop finishes.
        _worker = worker;
    }
        
    void VTLabelPlacementWorker::init(const std::shared_ptr<Layer>& layer, int delayTime) {
        schedule(layer, delayTime, false);
    }

    void VTLabelPlacementWorker::postpone(const std::shared_ptr<Layer>& layer, int delayTime) {
        schedule(layer, delayTime, true);
    }

    // 'postpone' pushes the deadline back on every call, so a stream of triggers yields one pass once it stops.
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
                    // Stamped at start under the claiming lock, or a schedule() during the pass reads a stale time.
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
    
    // maplibre's Placement.stillRecent gate: no new placement while the last one is fading.
    const int VTLabelPlacementWorker::MIN_PLACEMENT_INTERVAL = vt::LABEL_FADE_DURATION_MS;

    // CPU cap: after C ms the next pass waits C * (1000/TARGET - 1), i.e. TARGET ms of every second.
    static const double PLACEMENT_TARGET_MS_PER_SECOND = 90.0;

    // Slicing is off, see culler.beginSlice below; the machinery still works and a non-zero budget restores it.
    static const double PLACEMENT_BUDGET_MS = 0.0;

    bool VTLabelPlacementWorker::calculateVTLabelPlacement() {
        std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock();
        if (!mapRenderer) {
            return false;
        }

        std::vector<std::shared_ptr<Layer>> layers = mapRenderer->getLayers()->getAll();

        // Composite layers append their internal child layers here, in draw order.
        std::vector<std::shared_ptr<VectorTileLayer> > labelLayers;
        for (const std::shared_ptr<Layer>& layer : layers) {
            layer->collectLabelLayers(labelLayers);
        }

        // A cycle keeps its opening view (as mapbox freezes the transform); a moved camera restarts it.
        if (_cycleActive && mapRenderer->getViewState().getModelviewProjectionMat() != _cycleViewState.getModelviewProjectionMat()) {
            for (const std::shared_ptr<VectorTileLayer>& layer : labelLayers) {
                layer->_tileRenderer->restartLabelPlacement();
            }
            _cycleWrappedLayers.clear();
            _cycleMs = 0;
            _cycleActive = false;
            // Nothing left to fade from: the only case that snaps.
            _snapNextPlacement = true;
        }
        if (!_cycleActive) {
            _cycleViewState = mapRenderer->getViewState();
        }
        const ViewState& viewState = _cycleViewState;

        // Outlives a pass: clearing the collision grid mid-cycle would free slots already taken.
        if (!_culler) {
            _culler = std::make_unique<vt::LabelCuller>(Const::WORLD_SIZE);
        }
        vt::LabelCuller& culler = *_culler;
        if (!_cycleActive) {
            culler.reset();
        }
        // Internal units per metre at the focus: cosh(mercator y) is Mercator's 1/cos(latitude) stretch.
        {
            double latitude = viewState.getFocusPos()(1) * Const::PI * 2.0 / Const::WORLD_SIZE;
            double coshLatitude = std::cosh(latitude);
            culler.setMetersToInternal(Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE * coshLatitude);
        }
        // In multiples of the camera-to-focus distance (Options::setLabelViewDistance).
        culler.setLabelViewDistance(mapRenderer->getOptions()->getLabelViewDistance());
        // Not sliced (budget 0): a slice sorts only its subset, so early labels steal higher-priority slots.
        bool forced = _snapNextPlacement;
        _snapNextPlacement = false;
        culler.beginSlice(PLACEMENT_BUDGET_MS);
        std::chrono::steady_clock::time_point passStart = std::chrono::steady_clock::now();

        bool reversedOrder = mapRenderer->getOptions()->isLayersLabelsProcessedInReverseOrder();
        bool changed = false;
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

        // Only the cycle knows a continuation is owed; a still map stops waking this thread.
        _cycleActive = !finished;
        if (_cycleActive) {
            scheduleContinuation();
        } else {
            _cycleMs = 0;
            // Placed for a camera that has since moved: redo, cross-faded rather than snapped.
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

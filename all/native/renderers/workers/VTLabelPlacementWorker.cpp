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

    // The ceiling, and the only thing that enforces it. A cycle's SIZE cannot bound a RATE: once
    // placement got cheap enough to run whole, it simply ran more often, and 8 ms at 15 passes a
    // second is 120 ms/s again. So after spending C ms, the next pass waits
    // C * (1000/TARGET - 1), holding placement to TARGET ms of every second whatever the tilt, the
    // cycle size or how often the camera asks. It is a cap, not a quota - a still map spends none.
    static const double PLACEMENT_TARGET_MS_PER_SECOND = 90.0;

    // A cycle is no longer SLICED, at any size - see the note at culler.beginSlice below. The
    // machinery for it is still here and still correct (a cursor per layer, a culler and a frozen
    // view that outlive a pass); restoring a non-zero budget there turns it back on.
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

        // A cycle in progress keeps the view it was opened against: its collision grid is half
        // built for that screen, and RESUMING against a moved camera would place the rest of the
        // labels against it. mapbox freezes the transform for a cycle for the same reason.
        //
        // Once the camera HAS moved, though, every placement the cycle has left to make is for a
        // screen that is gone, and draining it first is what the user waits through - measured at up
        // to ~10 s of panning before the near field is labelled. So the stale cycle is ABANDONED
        // rather than finished: cursors back to the start, grid cleared, new view. That keeps the
        // invariant (a cycle is never resumed against a different view) and drops the wait.
        if (_cycleActive && mapRenderer->getViewState().getModelviewProjectionMat() != _cycleViewState.getModelviewProjectionMat()) {
            for (const std::shared_ptr<VectorTileLayer>& layer : labelLayers) {
                layer->_tileRenderer->restartLabelPlacement();
            }
            _cycleWrappedLayers.clear();
            _cycleMs = 0;
            _cycleActive = false;
            // The screen this cycle was placing for is gone, so the placement that replaces it has
            // nothing to fade FROM: commit it outright. This is the only case that snaps - see
            // 'forced' below.
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
        // How far labels may be placed, in multiples of the camera-to-focus distance. maplibre's
        // own cut is the default; a view along the ground needs it raised or turned off, since its
        // focus sits a few kilometres in front of a low camera and everything worth naming is past
        // five times that (Options::setLabelViewDistance).
        culler.setLabelViewDistance(mapRenderer->getOptions()->getLabelViewDistance());
        // NOT rationed within a pass any more. PLACEMENT_TARGET_MS_PER_SECOND is what holds
        // placement to its share of wall clock, and it does so "whatever the tilt, the cycle size or
        // how often the camera asks" - so a slice buys no ceiling the duty cycle does not already
        // give. What a slice DOES cost is the SELECTION: the culler sorts by priority and then
        // inserts greedily, and a slice sorts only the subset it collected, so a label collected
        // early claims a grid slot before a higher-priority label in a later slice is even looked
        // at. On a city map that is invisible - a cycle is ~8 ms there and was never sliced - but in
        // a panorama the whole screen is horizon band, a cycle is hundreds of milliseconds, and the
        // labels whose priority only counts once everything is in the sort are exactly the far
        // summits the mode exists to name. The set of names also changed with every slice boundary,
        // which is what reads as labels churning while the view turns.
        //
        // The price is latency, not throughput: a 200 ms cycle now lands in one pass and the duty
        // cycle spaces the next one ~2 s later, so placement follows a turning view in steps
        // instead of continuously. That is the trade the mode wants - one stable set of names.
        bool forced = _snapNextPlacement;
        _snapNextPlacement = false;
        culler.beginSlice(PLACEMENT_BUDGET_MS);
        std::chrono::steady_clock::time_point passStart = std::chrono::steady_clock::now();

        bool reversedOrder = mapRenderer->getOptions()->isLayersLabelsProcessedInReverseOrder();
        bool changed = false;
        // A layer wraps when its cursor reaches the end of its own label list, which under slicing
        // takes a different number of passes per layer - so requiring them all to wrap in the SAME
        // pass never came true, the cycle never ended and the grid was never cleared. Measured on the
        // Crosscall: 0 completions in 230 passes, 1241 stale records held forever, and no label of a
        // newly loaded tile could claim a slot again.
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
            // Only an ABANDONED cycle snaps. A redo owed to a camera that moved while the pass ran
            // is the normal case on a view that is being turned - snapping that one meant every
            // placement of a turning view arrived with no fade at all, which is what made a changed
            // name read as a blink rather than a cross-fade.
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
            // Every label of that cycle was placed against the view it opened with, so if the camera
            // has moved since, the screen now shows placements for a camera that is gone: far tiles
            // keep a mass of labels overlapping each other and the newly revealed ground has none.
            // Nothing else will ask for the redo - a still map stops waking this thread - so the
            // cycle asks for itself, and converges as soon as one opens on a camera that holds.
            // It does NOT snap: this is the ordinary case while a view is being turned, and the
            // cross-fade is what keeps the change legible.
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

#include "TouchHandler.h"
#include "components/Layers.h"
#include "components/Options.h"
#include "graphics/ViewState.h"
#include "terrain/ElevationManager.h"
#include "layers/Layer.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "renderers/MapRenderer.h"
#include "renderers/components/RayIntersectedElement.h"
#include "renderers/components/RayIntersectedElementComparator.h"
#include "renderers/cameraevents/CameraPanEvent.h"
#include "renderers/cameraevents/CameraRotationEvent.h"
#include "renderers/cameraevents/CameraTiltEvent.h"
#include "renderers/cameraevents/CameraZoomEvent.h"
#include "ui/MapClickInfo.h"
#include "ui/MapInteractionInfo.h"
#include "ui/MapEventListener.h"
#include "ui/workers/ClickHandlerWorker.h"
#include "utils/Const.h"
#include "utils/Log.h"

#include <algorithm>

namespace massif {

    TouchHandler::TouchHandler(const std::shared_ptr<MapRenderer>& mapRenderer, const std::shared_ptr<Options>& options) :
        _gestureMode(SINGLE_POINTER_CLICK_GUESS),
        _gestureAnchorHeight(0.0),
        _panScale(0.0),
        _prevScreenPos1(0, 0),
        _prevScreenPos2(0, 0),
        _swipe1(0, 0),
        _swipe2(0, 0),
        _cameraEvents(0),
        _pointersDown(0),
        _idling(true),
        _noDualPointerYet(true),
        _dualPointerReleaseTime(),
        _lookAnchored(false),
        _lookAnchorPos(0, 0),
        _lookAnchorHeading(0),
        _lookAnchorElevation(0),
        _lookRotation(0),
        _lookTilt(0),
        _lookSampleTime(),
        _mapEventListener(),
        _clickHandlerWorker(std::make_shared<ClickHandlerWorker>(options)),
        _clickHandlerThread(),
        _options(options),
        _mapRenderer(mapRenderer),
        _mapRendererListener(),
        _mutex(),
        _onTouchListeners(),
        _onTouchListenersMutex()
    {
    }
        
    TouchHandler::~TouchHandler() {
    }
        
    void TouchHandler::init() {
        _clickHandlerWorker->setComponents(shared_from_this(), _clickHandlerWorker);
        _clickHandlerThread = std::thread(std::ref(*_clickHandlerWorker));

        _mapRendererListener = std::make_shared<MapRendererListener>(shared_from_this());
        _mapRenderer->registerOnChangeListener(_mapRendererListener);
    }
    
    void TouchHandler::deinit() {
        _mapRenderer->unregisterOnChangeListener(_mapRendererListener);
        _mapRendererListener.reset();
        
        _clickHandlerWorker->stop();
        _clickHandlerThread.detach();
    }
    
    std::shared_ptr<MapEventListener> TouchHandler::getMapEventListener() const {
        return _mapEventListener.get();
    }
    
    void TouchHandler::setMapEventListener(const std::shared_ptr<MapEventListener>& mapEventListener) {
        _mapEventListener.set(mapEventListener);
    }
    
    void TouchHandler::onTouchEvent(int action, const ScreenPos& screenPos1, const ScreenPos& screenPos2) {
        std::vector<std::shared_ptr<OnTouchListener> > onTouchListeners;
        {
            std::lock_guard<std::mutex> lock(_onTouchListenersMutex);
            onTouchListeners = _onTouchListeners;
        }
        bool consumed = false;
        for (std::size_t i = onTouchListeners.size(); i-- > 0; ) {
            if (onTouchListeners[i]->onTouchEvent(action, screenPos1, screenPos2)) {
                consumed = true;
                break;
            }
        }

        if (!consumed) {
            handleTouchEvent(action, screenPos1, screenPos2);
        }

        // Runs even when a listener consumed the event, or a consumed UP leaves _pointersDown stuck and onMapStable dead.
        bool pointersDown = false;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            switch (action) {
            // Assigned, not incremented: a missed UP then costs one gesture, not every later onMapStable.
            case ACTION_POINTER_1_DOWN:
                _pointersDown = 1;
                break;
            case ACTION_POINTER_2_DOWN:
                _pointersDown = 2;
                break;
            case ACTION_POINTER_1_UP:
            case ACTION_POINTER_2_UP:
                _pointersDown = std::max(0, _pointersDown - 1);
                break;
            case ACTION_CANCEL:
                _pointersDown = 0;
                break;
            }
            pointersDown = _pointersDown > 0;
        }
        // Outside _mutex: the renderer's own lock is never taken under it.
        _mapRenderer->setTouchGestureActive(pointersDown);

        checkCameraEvents();
        checkMapStable();
    }

    void TouchHandler::handleTouchEvent(int action, const ScreenPos& screenPos1, const ScreenPos& screenPos2) {
        std::unique_lock<std::recursive_mutex> lock(_mutex);
        ViewState viewState = _mapRenderer->getViewState();
        switch (action) {
        case ACTION_POINTER_1_DOWN:
            if (!_clickHandlerWorker->isRunning()) {
                _clickHandlerWorker->init();
            }
            _clickHandlerWorker->pointer1Down(screenPos1);
            _noDualPointerYet = true;
            _lookAnchored = false;
            _lookAnchorPos = screenPos1;
            _mapRenderer->getKineticEventHandler().stopPan();
            _mapRenderer->getKineticEventHandler().stopRotation();
            _mapRenderer->getKineticEventHandler().stopZoom();
            _mapRenderer->getKineticEventHandler().stopLook();
            break;
    
        case ACTION_POINTER_2_DOWN:
            _noDualPointerYet = false;
            switch (_gestureMode) {
            case SINGLE_POINTER_CLICK_GUESS:
                _clickHandlerWorker->pointer2Down(screenPos2);
                _gestureMode = DUAL_POINTER_CLICK_GUESS;
                break;
            case SINGLE_POINTER_PAN:
            case SINGLE_POINTER_ZOOM:
                startDualPointer(screenPos1, screenPos2);
                break;
            default:
                break;
            }
            break;
    
        case ACTION_MOVE:
            switch (_gestureMode) {
            case SINGLE_POINTER_CLICK_GUESS:
                _clickHandlerWorker->pointer1Moved(screenPos1);
                break;
            case DUAL_POINTER_CLICK_GUESS:
                _clickHandlerWorker->pointer1Moved(screenPos1);
                _clickHandlerWorker->pointer2Moved(screenPos2);
                break;
            case SINGLE_POINTER_PAN:
                {
                    auto deltaTime = std::chrono::steady_clock::now() - _dualPointerReleaseTime;
                    if (deltaTime >= DUAL_STOP_HOLD_DURATION) {
                        // Free roam turns the one-finger drag into a look; panning moves to two fingers.
                        if (_options->getFreeRoamMode() != FreeRoamMode::FREE_ROAM_MODE_OFF) {
                            singlePointerLook(screenPos1, viewState);
                        } else {
                            singlePointerPan(screenPos1, viewState);
                        }
                    }
                }
                break;
            case SINGLE_POINTER_ZOOM:
                singlePointerZoom(screenPos1, viewState);
                break;
            case DUAL_POINTER_GUESS:
                dualPointerGuess(screenPos1, screenPos2, viewState);
                break;
            case DUAL_POINTER_MOVE:
                dualPointerMove(screenPos1, screenPos2, viewState);
                break;
            case DUAL_POINTER_TILT:
                dualPointerTilt(screenPos1, viewState);
                break;
            case DUAL_POINTER_ROTATE:
            case DUAL_POINTER_SCALE:
                if (_options->getPanningMode() == PanningMode::PANNING_MODE_STICKY) {
                    float factor = calculateRotatingScalingFactor(screenPos1, screenPos2);
                    if (factor > ROTATION_SCALING_FACTOR_THRESHOLD_STICKY) {
                        _gestureMode = DUAL_POINTER_ROTATE;
                    } else if (factor < -ROTATION_SCALING_FACTOR_THRESHOLD_STICKY) {
                        _gestureMode = DUAL_POINTER_SCALE;
                    }
                }
                dualPointerPan(screenPos1, screenPos2, _gestureMode == DUAL_POINTER_ROTATE, _gestureMode == DUAL_POINTER_SCALE, viewState);
                break;
            case DUAL_POINTER_FREE:
                dualPointerPan(screenPos1, screenPos2, true, true, viewState);
                break;
            }
            break;
    
        case ACTION_CANCEL:
            _clickHandlerWorker->cancel();
            _gestureMode = SINGLE_POINTER_CLICK_GUESS;
            break;

        case ACTION_POINTER_1_UP:
            switch (_gestureMode) {
            case SINGLE_POINTER_CLICK_GUESS:
                _clickHandlerWorker->pointer1Up();
                break;
            case DUAL_POINTER_CLICK_GUESS: {
                _clickHandlerWorker->pointer1Up();
                _gestureMode = SINGLE_POINTER_CLICK_GUESS;
                break;
            }
            case SINGLE_POINTER_PAN:
                _gestureMode = SINGLE_POINTER_CLICK_GUESS;
                // A first person drag glides on as a look, turning the view about the camera,
                // unless the finger rested before it lifted.
                if (_options->getFreeRoamMode() == FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON) {
                    if (_lookAnchored && std::chrono::steady_clock::now() - _lookSampleTime < LOOK_KINETIC_REST) {
                        _mapRenderer->getKineticEventHandler().startLook();
                    }
                    break;
                }
                if (_noDualPointerYet) {
                    _mapRenderer->getKineticEventHandler().startPan();
                } else {
                    auto deltaTime = std::chrono::steady_clock::now() - _dualPointerReleaseTime;
                    if (deltaTime < DUAL_KINETIC_HOLD_DURATION) {
                        _mapRenderer->getKineticEventHandler().startRotation();
                        _mapRenderer->getKineticEventHandler().startZoom();
                    }
                }
                break;
            case SINGLE_POINTER_ZOOM:
                if (singlePointerZoomStop(screenPos1, viewState)) {
                    lock.unlock();
                    doubleTapZoom(screenPos1, viewState);
                    lock.lock();
                }
                _gestureMode = SINGLE_POINTER_CLICK_GUESS;
                if (_noDualPointerYet) {
                    _mapRenderer->getKineticEventHandler().startZoom();
                }
                break;
            case DUAL_POINTER_GUESS:
            case DUAL_POINTER_TILT:
            case DUAL_POINTER_ROTATE:
            case DUAL_POINTER_SCALE:
            case DUAL_POINTER_FREE:
            case DUAL_POINTER_MOVE:
                _dualPointerReleaseTime = std::chrono::steady_clock::now();
                _lookAnchored = false;
                _lookAnchorPos = screenPos2;
                _prevScreenPos1 = screenPos2;
                _gestureMode = SINGLE_POINTER_PAN;
                updatePanScale(screenPos2, viewState); // a new pan starts here
                break;
            }
            break;
    
        case ACTION_POINTER_2_UP:
            switch (_gestureMode) {
            case DUAL_POINTER_CLICK_GUESS:
                _clickHandlerWorker->pointer2Up();
                _gestureMode = SINGLE_POINTER_CLICK_GUESS;
                break;
            case DUAL_POINTER_GUESS:
            case DUAL_POINTER_TILT:
            case DUAL_POINTER_ROTATE:
            case DUAL_POINTER_SCALE:
            case DUAL_POINTER_FREE:
            case DUAL_POINTER_MOVE:
                 _dualPointerReleaseTime = std::chrono::steady_clock::now();
                 _prevScreenPos1 = screenPos1;
                 _gestureMode = SINGLE_POINTER_PAN;
                 updatePanScale(screenPos1, viewState); // a new pan starts here
                 break;
            default:
                break;
            }
            break;
        }

    }

    void TouchHandler::onWheelEvent(int delta, const ScreenPos& screenPos) {
        onWheelZoom(delta * WHEEL_TICK_TO_ZOOM_DELTA, screenPos);
    }

    void TouchHandler::onWheelZoom(float zoomDelta, const ScreenPos& screenPos) {
        if (_options->isUserInput()) {
            ViewState viewState = _mapRenderer->getViewState();
            std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
            if (!projectionSurface) {
                return;
            }

            _mapRenderer->getAnimationHandler().stopPan();
            _mapRenderer->getAnimationHandler().stopRotation();
            _mapRenderer->getAnimationHandler().stopTilt();
            _mapRenderer->getAnimationHandler().stopZoom();
            _mapRenderer->getAnimationHandler().stopFlight();
            
            updateGestureAnchorHeight(screenPos, viewState);

            CameraZoomEvent cameraZoomTargetEvent;
            cameraZoomTargetEvent.setZoomDelta(zoomDelta);
            cameraZoomTargetEvent.setTargetPos(calculatePivotPos(screenPos, viewState));
            cameraZoomTargetEvent.setPinTarget(true);
            _mapRenderer->calculateCameraEvent(cameraZoomTargetEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);

            DirectorPtr<MapEventListener> mapEventListener = _mapEventListener;

            if (mapEventListener) {
                mapEventListener->onMapInteraction(std::make_shared<MapInteractionInfo>(false, true, false, false));
            }
        }
    }

    void TouchHandler::checkCameraEvents() {
        int cameraEvents = _cameraEvents.exchange(0);

        if (cameraEvents) {
            noteMapMoved(MapMoveReason::MAP_MOVE_REASON_GESTURE);

            DirectorPtr<MapEventListener> mapEventListener = _mapEventListener;

            if (mapEventListener) {
                mapEventListener->onMapMoved(MapMoveReason::MAP_MOVE_REASON_GESTURE);

                bool pan = (cameraEvents & CAMERA_PAN) != 0;
                bool zoom = (cameraEvents & CAMERA_ZOOM) != 0;
                bool rotate = (cameraEvents & CAMERA_ROTATE) != 0;
                bool tilt = (cameraEvents & CAMERA_TILT) != 0;
                mapEventListener->onMapInteraction(std::make_shared<MapInteractionInfo>(pan, zoom, rotate, tilt));
            }
        }
    }
    
    void TouchHandler::noteMapMoved(MapMoveReason::MapMoveReason reason) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _pendingMoveReason = reason;
    }

    void TouchHandler::checkMapStable() {
        bool atRest = !_mapRenderer->getKineticEventHandler().isPanning() && !_mapRenderer->getKineticEventHandler().isRotating() && !_mapRenderer->getKineticEventHandler().isZooming() && !_mapRenderer->getKineticEventHandler().isLooking();

        // Edge-triggered: taking the pending reason is the edge, so a second at-rest check stays quiet
        // and a touch that never moved the camera never reports.
        std::optional<MapMoveReason::MapMoveReason> reason;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            if (atRest && _pointersDown == 0 && _idling.load()) {
                std::swap(reason, _pendingMoveReason);
            }
        }

        if (reason) {
            DirectorPtr<MapEventListener> mapEventListener = _mapEventListener;

            if (mapEventListener) {
                mapEventListener->onMapStable(*reason);
            }
        }
    }
    
    float TouchHandler::calculateRotatingScalingFactor(const ScreenPos& screenPos1, const ScreenPos& screenPos2) const {
        cglib::vec2<float> prevDelta(_prevScreenPos1.getX() - _prevScreenPos2.getX(), _prevScreenPos1.getY() - _prevScreenPos2.getY());
        cglib::vec2<float> moveDelta(screenPos1.getX() - _prevScreenPos1.getX(), screenPos1.getY() - _prevScreenPos1.getY());
        double factor = 0.0;
        for (int i = 0; i < 2; i++) {
            if (cglib::length(prevDelta) > 0 && cglib::length(moveDelta) > 0) {
                float cos = std::abs(cglib::dot_product(moveDelta, prevDelta)) / cglib::length(moveDelta) / cglib::length(prevDelta);
                float sin = std::sqrt(1.0f - std::min(1.0f, cos * cos));
                float tan = sin / cos;
                factor += std::log(tan); // convert range [0, 1] to range [-inf, 0] and range [1, inf] to range [0, inf]
            }

            moveDelta = cglib::vec2<float>(screenPos2.getX() - _prevScreenPos2.getX(), screenPos2.getY() - _prevScreenPos2.getY());
        }
        return static_cast<float>(factor);
    }
    
    void TouchHandler::singlePointerPan(const ScreenPos& screenPos, const ViewState& viewState) {
        if (_options->isUserInput()) {
            _mapRenderer->getAnimationHandler().stopPan();
            _mapRenderer->getAnimationHandler().stopRotation();
            _mapRenderer->getAnimationHandler().stopTilt();
            _mapRenderer->getAnimationHandler().stopZoom();
            _mapRenderer->getAnimationHandler().stopFlight();

            panBetween(_prevScreenPos1, screenPos, viewState);
        }
        _prevScreenPos1 = screenPos;
    }

    // The one pan both the one- and two-finger gestures use, so they agree on speed mode and grazing-ray handling.
    void TouchHandler::panBetween(const ScreenPos& prevScreenPos, const ScreenPos& screenPos, const ViewState& viewState) {
        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface) {
            return;
        }

        float dx = screenPos.getX() - prevScreenPos.getX();
        float dy = screenPos.getY() - prevScreenPos.getY();
        if (dx == 0 && dy == 0) {
            return;
        }

        double panScale = _panScale.load();
        if (_options->getPanningSpeedMode() != PanningSpeedMode::PANNING_SPEED_MODE_MAP && panScale > 0) {
            // Screen delta at the gesture's starting scale: grabbing the world exactly would speed up a drag up the screen.
            cglib::vec3<double> focusPos = viewState.getFocusPos();
            MapPos focusMapPos = projectionSurface->calculateMapPos(focusPos);
            cglib::vec3<double> normal = projectionSurface->calculateNormal(focusMapPos);
            // Threshold, not '== 0': a gesture-reached vertical view leaves a ~1e-16 cross product that unit() turns into noise.
            cglib::vec3<double> right = cglib::vector_product(viewState.calculateViewDir(), normal);
            if (cglib::length(right) < VIEW_AXIS_EPSILON) {
                right = cglib::vector_product(viewState.getUpVec(), normal); // straight up or down
            }
            if (cglib::length(right) < VIEW_AXIS_EPSILON) {
                return;
            }
            right = cglib::unit(right);
            cglib::vec3<double> forward = cglib::vector_product(normal, right);
            if (cglib::length(forward) < VIEW_AXIS_EPSILON) {
                return;
            }
            forward = cglib::unit(forward);

            cglib::vec3<double> offset = forward * (dy * panScale) + right * (-dx * panScale);
            CameraPanEvent cameraEvent;
            cameraEvent.setPosDelta(std::make_pair(focusMapPos, projectionSurface->calculateMapPos(focusPos + offset)));
            _cameraEvents.fetch_or(CAMERA_PAN);
            _mapRenderer->calculateCameraEvent(cameraEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);
            return;
        }

        if (!isValidScreenPosition(screenPos, viewState) || !isValidScreenPosition(prevScreenPos, viewState)) {
            return;
        }
        MapPos currentPos = mapScreenPosition(screenPos, viewState);
        MapPos prevPos = mapScreenPosition(prevScreenPos, viewState);

        if (viewState.getTilt() < PAN_CLAMP_MAX_TILT) {
            // Tangram's guard (inputHandler.cpp getTranslation): near the horizon ground hits fly apart,
            // so cap the travel at what the pixels are worth at the map scale.
            cglib::vec3<double> pos0 = projectionSurface->calculatePosition(currentPos);
            cglib::vec3<double> pos1 = projectionSurface->calculatePosition(prevPos);
            double travel = projectionSurface->calculateDistance(pos0, pos1);
            double unitsPerPixel = 2.0 * viewState.calculateCameraDistance() * viewState.getTanHalfFOVY() / std::max(1, viewState.getHeight());
            double limit = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) * unitsPerPixel;
            if (limit > 0 && travel > limit) {
                cglib::mat4x4<double> transform = projectionSurface->calculateTranslateMatrix(pos0, pos1, limit / travel);
                prevPos = projectionSurface->calculateMapPos(cglib::transform_point(pos0, transform));
            }
        }

        CameraPanEvent cameraEvent;
        cameraEvent.setPosDelta(std::make_pair(currentPos, prevPos));
        _cameraEvents.fetch_or(CAMERA_PAN);
        _mapRenderer->calculateCameraEvent(cameraEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);
    }

    namespace {
        // Heading (clockwise from north) and elevation, radians, of the ray through screen offset (a, b)
        // (right and up, in focal lengths) for a camera at `heading` and `pitch` (up positive).
        void lookRayDirection(double a, double b, double heading, double pitch, double& rayHeading, double& rayElevation) {
            double forward = std::cos(pitch) - b * std::sin(pitch);
            rayHeading = heading + std::atan2(a, forward);
            rayElevation = std::atan2(std::sin(pitch) + b * std::cos(pitch), std::sqrt(a * a + forward * forward));
        }
    }

    void TouchHandler::firstPersonLook(const ScreenPos& screenPos, const ViewState& viewState) {
        // The direction under the finger at the look's start stays under it (peakfinder.com, geo-three),
        // solved against the camera the look asked for: the view state lags queued events by a frame.
        // Pitch is minus the tilt (90 = straight down); heading is minus the rotation.
        double focal = 0.5 * viewState.getHeight() / std::max(viewState.getTanHalfFOVY(), 1.0e-6);
        if (!(focal > 0)) {
            return;
        }
        auto offsets = [&](const ScreenPos& pos, double& a, double& b) {
            a = (pos.getX() - 0.5 * viewState.getWidth()) / focal;
            b = -(pos.getY() - 0.5 * viewState.getHeight()) / focal;
        };
        double a = 0, b = 0;
        if (!_lookAnchored) {
            _lookRotation = viewState.getRotation();
            _lookTilt = viewState.getTilt();
            offsets(_lookAnchorPos, a, b);
            lookRayDirection(a, b, -_lookRotation * Const::DEG_TO_RAD, -_lookTilt * Const::DEG_TO_RAD, _lookAnchorHeading, _lookAnchorElevation);
            _lookAnchored = true;
            _lookSampleTime = std::chrono::steady_clock::now();
        }
        offsets(screenPos, a, b);
        // Ray elevation is monotonic in pitch, so bisect within the tilt range.
        double pitchMin = -_options->getTiltRange().getMax() * Const::DEG_TO_RAD;
        double pitchMax = -_options->getTiltRange().getMin() * Const::DEG_TO_RAD;
        double low = pitchMin, high = pitchMax;
        double rayHeading = 0, rayElevation = 0;
        for (int i = 0; i < 40; i++) {
            double pitch = 0.5 * (low + high);
            lookRayDirection(a, b, 0.0, pitch, rayHeading, rayElevation);
            (rayElevation < _lookAnchorElevation ? low : high) = pitch;
        }
        double pitch = 0.5 * (low + high);
        lookRayDirection(a, b, 0.0, pitch, rayHeading, rayElevation);
        float rotation = static_cast<float>(-(_lookAnchorHeading - rayHeading) * Const::RAD_TO_DEG);
        float tilt = static_cast<float>(-pitch * Const::RAD_TO_DEG);

        float rotationDelta = std::remainder(rotation - _lookRotation, 360.0f);
        float tiltDelta = tilt - _lookTilt;
        _lookRotation = rotation;
        _lookTilt = tilt;

        auto now = std::chrono::steady_clock::now();
        float seconds = std::chrono::duration<float>(now - _lookSampleTime).count();
        _lookSampleTime = now;
        _mapRenderer->getKineticEventHandler().setLookDelta(rotationDelta, tiltDelta, seconds);

        if (rotationDelta != 0) {
            CameraRotationEvent cameraEvent;
            cameraEvent.setRotationDelta(rotationDelta);
            _cameraEvents.fetch_or(CAMERA_ROTATE);
            _mapRenderer->calculateCameraEvent(cameraEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_GESTURE);
        }
        if (tiltDelta != 0) {
            CameraTiltEvent cameraEvent;
            cameraEvent.setTiltDelta(tiltDelta);
            _cameraEvents.fetch_or(CAMERA_TILT);
            _mapRenderer->calculateCameraEvent(cameraEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_GESTURE);
        }
    }

    void TouchHandler::singlePointerLook(const ScreenPos& screenPos, const ViewState& viewState) {
        if (_options->isUserInput() && _options->getFreeRoamMode() == FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON) {
            _mapRenderer->getAnimationHandler().stopPan();
            _mapRenderer->getAnimationHandler().stopRotation();
            _mapRenderer->getAnimationHandler().stopTilt();
            _mapRenderer->getAnimationHandler().stopZoom();
            _mapRenderer->getAnimationHandler().stopFlight();
            firstPersonLook(screenPos, viewState);
            _prevScreenPos1 = screenPos;
            return;
        }
        if (_options->isUserInput()) {
            _mapRenderer->getAnimationHandler().stopPan();
            _mapRenderer->getAnimationHandler().stopRotation();
            _mapRenderer->getAnimationHandler().stopTilt();
            _mapRenderer->getAnimationHandler().stopZoom();
            _mapRenderer->getAnimationHandler().stopFlight();

            float dpi = _options->getDPI();
            float dx = screenPos.getX() - _prevScreenPos1.getX();
            float dy = screenPos.getY() - _prevScreenPos1.getY();

            // The orbiting 'look' mode. Rotates about the camera, not the focus: orbiting the focus
            // at a low tilt walks the camera through terrain.
            if (dx != 0) {
                std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
                CameraRotationEvent cameraEvent;
                float lookDelta = dx * _options->getFreeRoamLookSensitivity() / dpi;
                cameraEvent.setRotationDelta(lookDelta);
                if (projectionSurface) {
                    cameraEvent.setTargetPos(projectionSurface->calculateMapPos(viewState.getCameraPos()));
                }
                _cameraEvents.fetch_or(CAMERA_ROTATE);
                _mapRenderer->calculateCameraEvent(cameraEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_GESTURE);
            }
            // Opposite to the two-finger tilt: a look drags the view, not the ground (Street View convention).
            if (dy != 0) {
                float scale = -INCHES_TO_TILT_DELTA / dpi;
                if (_options->isTiltGestureReversed()) {
                    scale = -scale;
                }
                float tiltDelta = dy * scale;
                CameraTiltEvent cameraEvent;
                cameraEvent.setTiltDelta(tiltDelta);
                _cameraEvents.fetch_or(CAMERA_TILT);
                _mapRenderer->calculateCameraEvent(cameraEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_GESTURE);
            }
        }
        _prevScreenPos1 = screenPos;
    }

    void TouchHandler::singlePointerZoom(const ScreenPos& screenPos, const ViewState& viewState) {
        if (_options->isUserInput()) {
            _mapRenderer->getAnimationHandler().stopPan();
            _mapRenderer->getAnimationHandler().stopRotation();
            _mapRenderer->getAnimationHandler().stopTilt();
            _mapRenderer->getAnimationHandler().stopZoom();
            _mapRenderer->getAnimationHandler().stopFlight();
            
            // No ground hit required: zooms about the focus, so it also works where the fingers' rays miss the ground.
            float dpi = _options->getDPI();
            cglib::vec2<float> tempSwipe1(screenPos.getX() - _prevScreenPos1.getX(), screenPos.getY() - _prevScreenPos1.getY());
            _swipe1 += tempSwipe1 * (1.0f / dpi);

            float delta = INCHES_TO_ZOOM_DELTA * (screenPos.getY() - _prevScreenPos1.getY()) / dpi;

            CameraZoomEvent cameraEvent;
            cameraEvent.setZoomDelta(delta);
            _cameraEvents.fetch_or(CAMERA_ZOOM);
            _mapRenderer->calculateCameraEvent(cameraEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);
        }
        _prevScreenPos1 = screenPos;
    }

    bool TouchHandler::singlePointerZoomStop(const ScreenPos& screenPos, const ViewState& viewState) {
        bool zoom = false;
        if (_options->isUserInput()) {
            std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
            if (!projectionSurface) {
                return false;
            }

            if (cglib::length(_swipe1) < GUESS_SWIPE_ZOOM_THRESHOLD) {
                if (_options->getPivotMode() != PivotMode::PIVOT_MODE_TOUCHPOINT || isValidScreenPosition(screenPos, viewState)) {
                    zoom = true;
                }
            }
        }
        _prevScreenPos1 = screenPos;
        return zoom;
    }
    
    void TouchHandler::dualPointerGuess(const ScreenPos& screenPos1, const ScreenPos& screenPos2, const ViewState& viewState) {
        // If the pointers' y coordinates differ too much it's the general case or rotation
        float dpi = _options->getDPI();
        float deltaY = std::abs(screenPos1.getY() - screenPos2.getY()) / dpi;
        if (deltaY > GUESS_MAX_DELTA_Y_INCHES) {
            _gestureMode = DUAL_POINTER_FREE;
        } else {
            float prevSwipe1Length = cglib::length(_swipe1);
            float prevSwipe2Length = cglib::length(_swipe2);

            cglib::vec2<float> tempSwipe1(screenPos1.getX() - _prevScreenPos1.getX(), screenPos1.getY() - _prevScreenPos1.getY());
            _swipe1 += tempSwipe1 * (1.0f / dpi);
            cglib::vec2<float> tempSwipe2(screenPos2.getX() - _prevScreenPos2.getX(), screenPos2.getY() - _prevScreenPos2.getY());
            _swipe2 += tempSwipe2 * (1.0f / dpi);
            
            float swipe1Length = cglib::length(_swipe1);
            float swipe2Length = cglib::length(_swipe2);
    
            if (((swipe1Length > GUESS_MIN_SWIPE_LENGTH_OPPOSITE_INCHES && prevSwipe1Length > 0) ||
                 (swipe2Length > GUESS_MIN_SWIPE_LENGTH_OPPOSITE_INCHES && prevSwipe2Length > 0))
                && _swipe1(1) * _swipe2(1) <= 0) {
                _gestureMode = DUAL_POINTER_FREE;
            } else if ((swipe1Length > GUESS_MIN_SWIPE_LENGTH_SAME_INCHES ||
                        swipe2Length > GUESS_MIN_SWIPE_LENGTH_SAME_INCHES) 
                       && _swipe1(1) * _swipe2(1) > 0) {
                if (std::abs(_swipe1(0) / swipe1Length) > GUESS_SWIPE_ABS_COS_THRESHOLD ||
                    std::abs(_swipe2(0) / swipe2Length) > GUESS_SWIPE_ABS_COS_THRESHOLD) {
                    _gestureMode = DUAL_POINTER_FREE;
                } else {
                    _gestureMode = DUAL_POINTER_TILT;
                }
            }
        }
    
        // Detect rotation/scaling gesture if general panning mode is switched off
        if (_gestureMode == DUAL_POINTER_FREE && _options->getPanningMode() != PanningMode::PANNING_MODE_FREE) {
            float factor = calculateRotatingScalingFactor(screenPos1, screenPos2);
            if (factor > ROTATION_FACTOR_THRESHOLD) {
                _gestureMode = DUAL_POINTER_ROTATE;
            } else if (factor < -SCALING_FACTOR_THRESHOLD) {
                _gestureMode = DUAL_POINTER_SCALE;
            } else {
                _gestureMode = DUAL_POINTER_GUESS;
                return;
            }
        }
    
        switch (_gestureMode) {
        case DUAL_POINTER_ROTATE:
        case DUAL_POINTER_SCALE:
        case DUAL_POINTER_FREE:
            _prevScreenPos1 = screenPos1;
            _prevScreenPos2 = screenPos2;
            break;
        case DUAL_POINTER_GUESS:
        case DUAL_POINTER_TILT:
        default:
            _prevScreenPos1 = screenPos1;
            _prevScreenPos2 = screenPos2;
            break;
        }
    }
    
    void TouchHandler::dualPointerTilt(const ScreenPos& screenPos, const ViewState& viewState) {
        if (_options->isUserInput()) {
            _mapRenderer->getAnimationHandler().stopPan();
            _mapRenderer->getAnimationHandler().stopRotation();
            _mapRenderer->getAnimationHandler().stopTilt();
            _mapRenderer->getAnimationHandler().stopZoom();
            _mapRenderer->getAnimationHandler().stopFlight();
            
            float scale = INCHES_TO_TILT_DELTA / _options->getDPI();
            if (_options->isTiltGestureReversed()) {
                scale = -scale;
            }

            CameraTiltEvent cameraEvent;
            cameraEvent.setTiltDelta((screenPos.getY() - _prevScreenPos1.getY()) * scale);
            _cameraEvents.fetch_or(CAMERA_TILT);
            _mapRenderer->calculateCameraEvent(cameraEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_GESTURE);
        }
        _prevScreenPos1 = screenPos;
    }
    
    void TouchHandler::dualPointerMove(const ScreenPos& screenPos1, const ScreenPos& screenPos2, const ViewState& viewState) {
        if (_options->isUserInput()) {
            std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
            if (!projectionSurface) {
                return;
            }

            _mapRenderer->getAnimationHandler().stopPan();
            _mapRenderer->getAnimationHandler().stopRotation();
            _mapRenderer->getAnimationHandler().stopTilt();
            _mapRenderer->getAnimationHandler().stopZoom();
            _mapRenderer->getAnimationHandler().stopFlight();

            // First person movement keeps height, heading and zoom; nothing is anchored to the ground,
            // so it works with the view aimed at the sky.
            float dx = (screenPos1.getX() + screenPos2.getX()) * 0.5f - (_prevScreenPos1.getX() + _prevScreenPos2.getX()) * 0.5f;
            float dy = (screenPos1.getY() + screenPos2.getY()) * 0.5f - (_prevScreenPos1.getY() + _prevScreenPos2.getY()) * 0.5f;
            _prevScreenPos1 = screenPos1;
            _prevScreenPos2 = screenPos2;
            if (dx == 0 && dy == 0) {
                return;
            }

            cglib::vec3<double> cameraPos = viewState.getCameraPos();
            MapPos cameraMapPos = projectionSurface->calculateMapPos(cameraPos);
            cglib::vec3<double> normal = projectionSurface->calculateNormal(cameraMapPos);
            cglib::vec3<double> viewDir = viewState.calculateViewDir();
            // Threshold, not '== 0' - see panBetween.
            cglib::vec3<double> right = cglib::vector_product(viewDir, normal);
            if (cglib::length(right) < VIEW_AXIS_EPSILON) {
                right = cglib::vector_product(viewState.getUpVec(), normal); // looking straight up or down
            }
            if (cglib::length(right) < VIEW_AXIS_EPSILON) {
                return;
            }
            right = cglib::unit(right);
            cglib::vec3<double> forward = cglib::vector_product(normal, right);
            if (cglib::length(forward) < VIEW_AXIS_EPSILON) {
                return;
            }
            forward = cglib::unit(forward);

            double cameraDistance = viewState.calculateCameraDistance();
            double viewHeight = viewState.getHeight();
            double perPixel = (viewHeight > 0
                ? 2.0 * std::tan(viewState.getHalfFOVY() * Const::DEG_TO_RAD) * cameraDistance / viewHeight
                : 0.0) * _options->getFreeRoamMoveSpeed();
            cglib::vec3<double> offset = forward * (dy * perPixel) + right * (-dx * perPixel);

            CameraPanEvent cameraEvent;
            cameraEvent.setPosDelta(std::make_pair(cameraMapPos, projectionSurface->calculateMapPos(cameraPos + offset)));
            _cameraEvents.fetch_or(CAMERA_PAN);
            _mapRenderer->calculateCameraEvent(cameraEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);
        }
    }

    void TouchHandler::dualPointerPan(const ScreenPos& screenPos1, const ScreenPos& screenPos2, bool rotate, bool scale, const ViewState& viewState) {
        if (_options->isUserInput()) {
            std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
            if (!projectionSurface) {
                return;
            }

            _mapRenderer->getAnimationHandler().stopPan();
            _mapRenderer->getAnimationHandler().stopRotation();
            _mapRenderer->getAnimationHandler().stopTilt();
            _mapRenderer->getAnimationHandler().stopZoom();
            _mapRenderer->getAnimationHandler().stopFlight();

            // Scale and angle from the screen, as tangram does: ground hits of a grazing ray turn a few pixels into a wild zoom or spin.
            cglib::vec2<float> currentVec(screenPos2.getX() - screenPos1.getX(), screenPos2.getY() - screenPos1.getY());
            cglib::vec2<float> prevVec(_prevScreenPos2.getX() - _prevScreenPos1.getX(), _prevScreenPos2.getY() - _prevScreenPos1.getY());
            double currentDist = cglib::length(currentVec);
            double prevDist = cglib::length(prevVec);

            ScreenPos currentMiddlePos((screenPos1.getX() + screenPos2.getX()) * 0.5f, (screenPos1.getY() + screenPos2.getY()) * 0.5f);
            ScreenPos prevMiddlePos((_prevScreenPos1.getX() + _prevScreenPos2.getX()) * 0.5f, (_prevScreenPos1.getY() + _prevScreenPos2.getY()) * 0.5f);

            MapPos pivotPos = calculatePivotPos(currentMiddlePos, viewState);

            if (_options->getPivotMode() == PivotMode::PIVOT_MODE_TOUCHPOINT) {
                panBetween(prevMiddlePos, currentMiddlePos, viewState);
            }

            if (scale && prevDist > 0 && currentDist > 0) {
                CameraZoomEvent cameraZoomTargetEvent;
                cameraZoomTargetEvent.setScale(static_cast<float>(prevDist / currentDist));
                cameraZoomTargetEvent.setTargetPos(pivotPos);
                cameraZoomTargetEvent.setPinTarget(true);
                _cameraEvents.fetch_or(CAMERA_ZOOM);
                _mapRenderer->calculateCameraEvent(cameraZoomTargetEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);
            }

            if (rotate && _options->isRotationGestures() && prevDist > 0 && currentDist > 0) {
                // Screen y points down, so a clockwise finger turn gives a positive map rotation.
                double cross = static_cast<double>(prevVec(0)) * currentVec(1) - static_cast<double>(prevVec(1)) * currentVec(0);
                double dot = static_cast<double>(prevVec(0)) * currentVec(0) + static_cast<double>(prevVec(1)) * currentVec(1);
                CameraRotationEvent cameraRotateTargetEvent;
                cameraRotateTargetEvent.setRotationDelta(static_cast<float>(std::atan2(cross, dot) * Const::RAD_TO_DEG));
                cameraRotateTargetEvent.setTargetPos(pivotPos);
                _cameraEvents.fetch_or(CAMERA_ROTATE);
                _mapRenderer->calculateCameraEvent(cameraRotateTargetEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);
            }
        }
    
        _prevScreenPos1 = screenPos1;
        _prevScreenPos2 = screenPos2;
    }

    void TouchHandler::doubleTapZoom(const ScreenPos& screenPos, const ViewState& viewState) {
        if (!_options->isUserInput()) {
            return;
        }

        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface) {
            return;
        }

        updateGestureAnchorHeight(screenPos, viewState);

        CameraZoomEvent cameraZoomTargetEvent;
        cameraZoomTargetEvent.setZoomDelta(1.0f);
        cameraZoomTargetEvent.setTargetPos(calculatePivotPos(screenPos, viewState));
        cameraZoomTargetEvent.setPinTarget(true);
        _mapRenderer->calculateCameraEvent(cameraZoomTargetEvent, ZOOM_GESTURE_ANIMATION_DURATION.count() / 1000.0f, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);

        DirectorPtr<MapEventListener> mapEventListener = _mapEventListener;

        if (mapEventListener) {
            // NOTE: animated action
            mapEventListener->onMapInteraction(std::make_shared<MapInteractionInfo>(false, true, false, false, true));
        }
    }
    
    void TouchHandler::click(const ScreenPos& screenPos, const std::chrono::milliseconds& duration) {
        if (!_options->isUserInput()) {
            return;
        }
        
        _mapRenderer->getAnimationHandler().stopPan();
        _mapRenderer->getAnimationHandler().stopRotation();
        _mapRenderer->getAnimationHandler().stopTilt();
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getAnimationHandler().stopFlight();
        
        ClickInfo clickInfo(ClickType::CLICK_TYPE_SINGLE, static_cast<float>(duration.count()) / 1000.0f);
        handleClick(clickInfo, screenPos);
    }
    
    void TouchHandler::longClick(const ScreenPos& screenPos, const std::chrono::milliseconds& duration) {
        if (!_options->isUserInput()) {
            return;
        }
        
        _mapRenderer->getAnimationHandler().stopPan();
        _mapRenderer->getAnimationHandler().stopRotation();
        _mapRenderer->getAnimationHandler().stopTilt();
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getAnimationHandler().stopFlight();

        auto longClickDuration = std::chrono::milliseconds(static_cast<int>(_options->getLongClickDuration() * 1000.0f));
        if (_options->isClickTypeDetection() && duration >= longClickDuration) {
            startSinglePointer(screenPos);
            ClickInfo clickInfo(ClickType::CLICK_TYPE_LONG, static_cast<float>(duration.count()) / 1000.0f);
            handleClick(clickInfo, screenPos);
        } else {
            ClickInfo clickInfo(ClickType::CLICK_TYPE_SINGLE, static_cast<float>(duration.count()) / 1000.0f);
            handleClick(clickInfo, screenPos);
        }
    }
    
    void TouchHandler::doubleClick(const ScreenPos& screenPos, const std::chrono::milliseconds& duration) {
        if (!_options->isUserInput()) {
            return;
        }
        
        _mapRenderer->getAnimationHandler().stopPan();
        _mapRenderer->getAnimationHandler().stopRotation();
        _mapRenderer->getAnimationHandler().stopTilt();
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getAnimationHandler().stopFlight();

        if (_options->isZoomGestures()) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _swipe1 = cglib::vec2<float>(0, 0);
            _prevScreenPos1 = screenPos;
            _gestureMode = SINGLE_POINTER_ZOOM;
        } else if (_options->isClickTypeDetection()) {
            ClickInfo clickInfo(ClickType::CLICK_TYPE_DOUBLE, static_cast<float>(duration.count()) / 1000.0f);
            handleClick(clickInfo, screenPos);
        } else {
            ClickInfo clickInfo(ClickType::CLICK_TYPE_SINGLE, static_cast<float>(duration.count()) / 1000.0f);
            handleClick(clickInfo, screenPos);
        }
    }
    
    void TouchHandler::dualClick(const ScreenPos& screenPos1, const ScreenPos& screenPos2, const std::chrono::milliseconds& duration) {
        if (!_options->isUserInput()) {
            return;
        }
        
        _mapRenderer->getAnimationHandler().stopPan();
        _mapRenderer->getAnimationHandler().stopRotation();
        _mapRenderer->getAnimationHandler().stopTilt();
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getAnimationHandler().stopFlight();

        if (_options->isZoomGestures()) {
            CameraZoomEvent cameraZoomTargetEvent;
            cameraZoomTargetEvent.setZoomDelta(-1.0f);
            cameraZoomTargetEvent.setTargetPos(_mapRenderer->getProjectionSurface()->calculateMapPos(_mapRenderer->getViewState().getFocusPos()));
            _mapRenderer->calculateCameraEvent(cameraZoomTargetEvent, ZOOM_GESTURE_ANIMATION_DURATION.count() / 1000.0f, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);

            DirectorPtr<MapEventListener> mapEventListener = _mapEventListener;

            if (mapEventListener) {
                // NOTE: animated action
                mapEventListener->onMapInteraction(std::make_shared<MapInteractionInfo>(false, true, false, false, true));
            }
        } else if (_options->isClickTypeDetection()) {
            ScreenPos centreScreenPos((screenPos1.getX() + screenPos2.getX()) / 2, (screenPos1.getY() + screenPos2.getY()) / 2);
            ClickInfo clickInfo(ClickType::CLICK_TYPE_DUAL, static_cast<float>(duration.count()) / 1000.0f);
            handleClick(clickInfo, centreScreenPos);
        } else {
            ClickInfo clickInfo(ClickType::CLICK_TYPE_SINGLE, static_cast<float>(duration.count()) / 1000.0f);
            handleClick(clickInfo, screenPos1);
        }
    }
    
    bool TouchHandler::isValidScreenPosition(const ScreenPos& screenPos, const ViewState& viewState) const {
        if (!viewState.getProjectionSurface()) {
            return false;
        }
        // Test the gesture's anchor plane (as mapScreenPosition does), not sea level, which is far off in mountains.
        cglib::vec3<double> pos = viewState.screenToWorld(cglib::vec2<float>(screenPos.getX(), screenPos.getY()), _gestureAnchorHeight.load());
        if (std::isnan(cglib::norm(pos))) {
            return false;
        }
        cglib::vec3<double> zVec = cglib::unit(viewState.getFocusPos() - viewState.getCameraPos());
        double dist = cglib::dot_product(zVec, pos - viewState.getCameraPos());
        return dist > 0 && dist < viewState.getFar();
    }

    cglib::ray3<double> TouchHandler::calculateScreenRay(const ScreenPos& screenPos, const ViewState& viewState) const {
        // ViewState::screenToWorld's unprojection stopped at the ray, valid whether or not it meets the ground.
        if (viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            double nan = std::numeric_limits<double>::quiet_NaN();
            return cglib::ray3<double>(viewState.getCameraPos(), cglib::vec3<double>(nan, nan, nan));
        }
        cglib::mat4x4<double> invMVP = cglib::inverse(viewState.getModelviewProjectionMat());
        double x = screenPos.getX() / viewState.getWidth() * 2 - 1;
        double y = 1 - screenPos.getY() / viewState.getHeight() * 2;
        cglib::vec3<double> near = cglib::transform_point(cglib::vec3<double>(x, y, -1), invMVP);
        cglib::vec3<double> far = cglib::transform_point(cglib::vec3<double>(x, y, 1), invMVP);
        return cglib::ray3<double>(near, far - near);
    }

    void TouchHandler::updatePanScale(const ScreenPos& screenPos, const ViewState& viewState) {
        _panScale.store(calculatePanScale(screenPos, viewState));
    }

    // Map units per screen pixel where the gesture starts; fixes the pan speed for the whole gesture.
    double TouchHandler::calculatePanScale(const ScreenPos& screenPos, const ViewState& viewState) const {
        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface || viewState.getHeight() <= 0) {
            return 0;
        }
        // Cap at a far-plane pixel: near the horizon the sample rays' ground hits fly apart.
        double maxScale = viewState.getFar() * 2.0 * viewState.getTanHalfFOVY() / viewState.getHeight();

        ScreenPos samplePos = screenPos;
        if (_options->getPanningSpeedMode() == PanningSpeedMode::PANNING_SPEED_MODE_CONSTANT) {
            samplePos = ScreenPos(viewState.getHalfWidth(), viewState.getHalfHeight());
        }
        double height = _gestureAnchorHeight.load();
        for (int attempt = 0; attempt < 2; attempt++) {
            cglib::vec3<double> pos0 = viewState.screenToWorld(cglib::vec2<float>(samplePos.getX(), samplePos.getY()), height);
            cglib::vec3<double> pos1 = viewState.screenToWorld(cglib::vec2<float>(samplePos.getX(), samplePos.getY() + 1), height);
            if (std::isfinite(cglib::norm(pos0)) && std::isfinite(cglib::norm(pos1))) {
                double scale = projectionSurface->calculateDistance(pos0, pos1);
                if (scale > 0) {
                    return std::min(scale, maxScale);
                }
            }
            // No ground under the touch: fall back to the screen centre, then to the far plane.
            samplePos = ScreenPos(viewState.getHalfWidth(), viewState.getHalfHeight());
        }
        return maxScale;
    }

    MapPos TouchHandler::mapScreenPosition(const ScreenPos& screenPos, const ViewState& viewState) const {
        cglib::vec3<double> pos = viewState.screenToWorld(cglib::vec2<float>(screenPos.getX(), screenPos.getY()), _gestureAnchorHeight.load());
        return viewState.getProjectionSurface()->calculateMapPos(pos);
    }

    // A missing ground hit falls back to the focus rather than cancelling the gesture (camera close to terrain).
    MapPos TouchHandler::calculatePivotPos(const ScreenPos& screenPos, const ViewState& viewState) const {
        if (_options->getPivotMode() == PivotMode::PIVOT_MODE_TOUCHPOINT && isValidScreenPosition(screenPos, viewState)) {
            return mapScreenPosition(screenPos, viewState);
        }
        return viewState.getProjectionSurface()->calculateMapPos(viewState.getFocusPos());
    }

    double TouchHandler::calculateTerrainHeight(const ScreenPos& screenPos, const ViewState& viewState) const {
        if (_options->getRenderProjectionMode() != RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
            return 0;
        }
        std::shared_ptr<TerrainOptions> terrainOptions = _options->getTerrainOptions();
        if (!terrainOptions || !terrainOptions->isActive()) {
            return 0;
        }

        cglib::vec3<double> worldPos = viewState.screenToWorld(cglib::vec2<float>(screenPos.getX(), screenPos.getY()), 0);
        if (std::isnan(cglib::norm(worldPos))) {
            return 0;
        }
        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
        cglib::ray3<double> ray(viewState.getCameraPos(), worldPos - viewState.getCameraPos());
        double t = 0;
        if (elevationManager->intersectRay(ray, t) && t > 0) {
            return ray(t)(2);
        }
        // No hit (camera under terrain, or DEM not decoded yet): anchor on the focus height, not sea level.
        double focusHeight = 0;
        const cglib::vec3<double>& focusPos = viewState.getFocusPos();
        if (elevationManager->getDisplayHeightCached(focusPos(0), focusPos(1), focusHeight)) {
            return focusHeight;
        }
        return 0;
    }

    void TouchHandler::updateGestureAnchorHeight(const ScreenPos& screenPos, const ViewState& viewState) {
        _gestureAnchorHeight.store(calculateTerrainHeight(screenPos, viewState));
    }

    void TouchHandler::handleClick(const ClickInfo& clickInfo, const ScreenPos& screenPos) {
        ViewState viewState = _mapRenderer->getViewState();
        // A touch aimed at the sky has no ground position but still hits sky-anchored layers (CelestialLayer) by ray.
        bool groundHit = isValidScreenPosition(screenPos, viewState);
        std::vector<RayIntersectedElement> results;
        MapPos mapPos;
        cglib::ray3<double> ray;
        if (groundHit) {
            updateGestureAnchorHeight(screenPos, viewState);
            mapPos = mapScreenPosition(screenPos, viewState);
            _mapRenderer->calculateRayIntersectedElements(mapPos, viewState, results);
        } else {
            ray = calculateScreenRay(screenPos, viewState);
            if (std::isnan(cglib::norm(ray.direction))) {
                return;
            }
            _mapRenderer->calculateRayIntersectedElements(ray, viewState, results);
        }
    
        // Sort the results but do 'reverse stable sort' to be consistent with the rendering order
        std::stable_sort(results.begin(), results.end(), RayIntersectedElementComparator(viewState));
        std::reverse(results.begin(), results.end());

        for (const RayIntersectedElement& intersectedElement : results) {
            if (intersectedElement.getLayer()->processClick(clickInfo, intersectedElement, viewState)) {
                return;
            }
        }

        // Click was ignored by layers, call map event listener, unless it never reached the ground:
        // then the sky layers report it instead, having no ground position to give - unless something was hit.
        if (!groundHit) {
            if (!results.empty()) {
                return;
            }
            if (std::shared_ptr<Layers> layers = _mapRenderer->getLayers()) {
                for (const std::shared_ptr<Layer>& layer : layers->getAll()) {
                    if (layer->processSkyClick(clickInfo, ray, viewState)) {
                        return;
                    }
                }
            }
            return;
        }
        DirectorPtr<MapEventListener> mapEventListener = _mapEventListener;

        if (mapEventListener) {
            mapEventListener->onMapClicked(std::make_shared<MapClickInfo>(clickInfo, _options->getBaseProjection()->fromInternal(mapPos)));
        }
    }

    void TouchHandler::startSinglePointer(const ScreenPos& screenPos) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _prevScreenPos1 = screenPos;
        _gestureMode = SINGLE_POINTER_PAN;
        ViewState viewState = _mapRenderer->getViewState();
        updateGestureAnchorHeight(screenPos, viewState);
        updatePanScale(screenPos, viewState);
    }

    void TouchHandler::startDualPointer(const ScreenPos& screenPos1, const ScreenPos& screenPos2) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _swipe1 = cglib::vec2<float>(0, 0);
        _swipe2 = cglib::vec2<float>(0, 0);
        _prevScreenPos1 = screenPos1;
        _prevScreenPos2 = screenPos2;
        // First person has no pinch or rotation, so there is nothing to guess.
        _gestureMode = (_options->getFreeRoamMode() == FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON ? DUAL_POINTER_MOVE : DUAL_POINTER_GUESS);
        ScreenPos middlePos((screenPos1.getX() + screenPos2.getX()) * 0.5f, (screenPos1.getY() + screenPos2.getY()) * 0.5f);
        ViewState viewState = _mapRenderer->getViewState();
        updateGestureAnchorHeight(middlePos, viewState);
        updatePanScale(middlePos, viewState); // the two-finger pan goes through the same speed mode
    }

    void TouchHandler::registerOnTouchListener(const std::shared_ptr<OnTouchListener>& listener) {
        {
            std::lock_guard<std::mutex> lock(_onTouchListenersMutex);
            _onTouchListeners.push_back(listener);
        }
    }

    void TouchHandler::unregisterOnTouchListener(const std::shared_ptr<OnTouchListener>& listener) {
        {
            std::lock_guard<std::mutex> lock(_onTouchListenersMutex);
            _onTouchListeners.erase(std::remove(_onTouchListeners.begin(), _onTouchListeners.end(), listener), _onTouchListeners.end());
        }
    }
    
    TouchHandler::MapRendererListener::MapRendererListener(const std::shared_ptr<TouchHandler>& touchHandler) : _touchHandler(touchHandler) {
    }
    
    void TouchHandler::MapRendererListener::onMapChanged(MapMoveReason::MapMoveReason reason) {
        if (auto touchHandler = _touchHandler.lock()) {
            touchHandler->noteMapMoved(reason);
            // No _mutex: the render thread holds the renderer lock, and a gesture holds _mutex while asking for the view state.
            touchHandler->_idling.store(false);
            if (touchHandler->_cameraEvents.load()) {
                return; // postpone listener call, will be called together with onMapInteraction
            }

            DirectorPtr<MapEventListener> mapEventListener = touchHandler->_mapEventListener;

            if (mapEventListener) {
                mapEventListener->onMapMoved(reason);
            }
        }
    }
    
    void TouchHandler::MapRendererListener::onMapIdle() {
        if (auto touchHandler = _touchHandler.lock()) {
            touchHandler->_idling.store(true);

            DirectorPtr<MapEventListener> mapEventListener = touchHandler->_mapEventListener;

            if (mapEventListener) {
                mapEventListener->onMapIdle();
            }
            touchHandler->checkMapStable();
        }
    }
    
    const float TouchHandler::GUESS_MAX_DELTA_Y_INCHES = 2.5f;
    const float TouchHandler::GUESS_MIN_SWIPE_LENGTH_SAME_INCHES = 0.2f;
    const float TouchHandler::GUESS_MIN_SWIPE_LENGTH_OPPOSITE_INCHES = 0.06f;
    
    const float TouchHandler::GUESS_SWIPE_ABS_COS_THRESHOLD = 0.707f;

    const float TouchHandler::GUESS_SWIPE_ZOOM_THRESHOLD = 0.06f;
    
    const float TouchHandler::SCALING_FACTOR_THRESHOLD = 0.5f;
    const float TouchHandler::ROTATION_FACTOR_THRESHOLD = 0.75f; // make rotation harder to trigger compared to scaling
    const float TouchHandler::ROTATION_SCALING_FACTOR_THRESHOLD_STICKY = 3.0f;

    const float TouchHandler::WHEEL_TICK_TO_ZOOM_DELTA = 0.25f;
    
    const float TouchHandler::INCHES_TO_TILT_DELTA = 32.0f;

    const double TouchHandler::VIEW_AXIS_EPSILON = 1.0e-3;

    const float TouchHandler::INCHES_TO_ZOOM_DELTA = 1.0f;

    const float TouchHandler::PAN_CLAMP_MAX_TILT = 15.0f; // tangram's pitch > 75 degrees
        
    const std::chrono::milliseconds TouchHandler::DUAL_KINETIC_HOLD_DURATION = std::chrono::milliseconds(100);

    const std::chrono::milliseconds TouchHandler::DUAL_STOP_HOLD_DURATION = std::chrono::milliseconds(75);
    // A finger that rested this long before lifting meant to stop there: no glide.
    const std::chrono::milliseconds TouchHandler::LOOK_KINETIC_REST = std::chrono::milliseconds(80);

    const std::chrono::milliseconds TouchHandler::ZOOM_GESTURE_ANIMATION_DURATION = std::chrono::milliseconds(250);

}

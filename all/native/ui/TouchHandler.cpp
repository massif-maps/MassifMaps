#include "TouchHandler.h"
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

        // The pointer count and the event checks run whether or not a listener took the gesture.
        // A consumed UP used to skip them, leaving _pointersDown stuck and onMapStable dead for good.
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            switch (action) {
            // Assigned, not incremented: a gesture start resyncs the count, so an UP the platform
            // never delivered costs one gesture instead of every onMapStable that follows.
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
        }

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
            _mapRenderer->getKineticEventHandler().stopPan();
            _mapRenderer->getKineticEventHandler().stopRotation();
            _mapRenderer->getKineticEventHandler().stopZoom();
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
                        // Free roam turns the one-finger drag into a look: panning moves to two
                        // fingers, which dualPointerPan already does.
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
                // A first person drag is a look, and a look does not glide on after the finger
                // leaves: every kinetic handler pans, rotates or zooms the MAP.
                if (_options->getFreeRoamMode() == FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON) {
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
                _prevScreenPos1 = screenPos2;
                _gestureMode = SINGLE_POINTER_PAN;
                updatePanScale(screenPos2, viewState); // a new pan starts here: a new speed
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
                 updatePanScale(screenPos1, viewState); // a new pan starts here: a new speed
                 break;
            default:
                break;
            }
            break;
        }

    }

    void TouchHandler::onWheelEvent(int delta, const ScreenPos& screenPos) {
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
            
            updateGestureAnchorHeight(screenPos, viewState);

            CameraZoomEvent cameraZoomTargetEvent;
            cameraZoomTargetEvent.setZoomDelta(delta * WHEEL_TICK_TO_ZOOM_DELTA);
            cameraZoomTargetEvent.setTargetPos(calculatePivotPos(screenPos, viewState));
            _mapRenderer->calculateCameraEvent(cameraZoomTargetEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);

            DirectorPtr<MapEventListener> mapEventListener = _mapEventListener;

            if (mapEventListener) {
                mapEventListener->onMapInteraction(std::make_shared<MapInteractionInfo>(false, true, false, false));
            }
        }
    }

    void TouchHandler::checkCameraEvents() {
        int cameraEvents = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            std::swap(cameraEvents, _cameraEvents);
        }

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
        bool atRest = !_mapRenderer->getKineticEventHandler().isPanning() && !_mapRenderer->getKineticEventHandler().isRotating() && !_mapRenderer->getKineticEventHandler().isZooming();

        // Edge-triggered: the end of a movement, reported once, with what caused it. Taking the
        // reason IS the edge - a second at-rest check finds nothing pending and stays quiet, and a
        // touch that never moved the camera never sets one.
        std::optional<MapMoveReason::MapMoveReason> reason;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            if (atRest && _pointersDown == 0 && _idling) {
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

            panBetween(_prevScreenPos1, screenPos, viewState);
        }
        _prevScreenPos1 = screenPos;
    }

    /**
     * Move the map so that what was under prevScreenPos ends up under screenPos - the one pan
     * both the one-finger drag and the two-finger gesture go through, so they cannot disagree
     * about the speed mode or about what a grazing ray is allowed to do.
     */
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
            // The pan travels the SCREEN delta at the scale the gesture started with. Grabbing
            // the world exactly - the other mode - re-derives that scale from wherever the
            // finger is now, so a drag that starts near the camera and travels up the screen
            // speeds up as it goes, which is not something the hand asked for.
            cglib::vec3<double> focusPos = viewState.getFocusPos();
            MapPos focusMapPos = projectionSurface->calculateMapPos(focusPos);
            cglib::vec3<double> normal = projectionSurface->calculateNormal(focusMapPos);
            // NOT '== 0': looking straight down, this cross product is meant to collapse and
            // hand over to the up vector - but a tilt REACHED BY GESTURE is vertical only to
            // within rounding, so it comes out at ~1e-16 instead of 0, the hand-over is missed,
            // and unit() then turns pure floating point noise into a unit vector pointing
            // anywhere. That is a pan that goes sideways when the finger goes up. Setting the
            // tilt to 90 outright happens to build the camera exactly vertical, which is why
            // only the gesture shows it.
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

            // Dragging the world down brings what was beyond the top edge into view, i.e. the
            // camera goes forward; dragging it right takes the camera left.
            cglib::vec3<double> offset = forward * (dy * panScale) + right * (-dx * panScale);
            CameraPanEvent cameraEvent;
            cameraEvent.setPosDelta(std::make_pair(focusMapPos, projectionSurface->calculateMapPos(focusPos + offset)));
            _cameraEvents |= CAMERA_PAN;
            _mapRenderer->calculateCameraEvent(cameraEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);
            return;
        }

        if (!isValidScreenPosition(screenPos, viewState) || !isValidScreenPosition(prevScreenPos, viewState)) {
            return;
        }
        MapPos currentPos = mapScreenPosition(screenPos, viewState);
        MapPos prevPos = mapScreenPosition(prevScreenPos, viewState);

        if (viewState.getTilt() < PAN_CLAMP_MAX_TILT) {
            // Tangram's guard (inputHandler.cpp getTranslation): near the horizon the two rays run
            // almost parallel to the ground and their hit points fly apart, so a finger travel of a
            // few pixels comes out as kilometres. Cap the travel at what those pixels are worth at
            // the map scale - the pan stops grabbing exactly, which is the point.
            cglib::vec3<double> pos0 = projectionSurface->calculatePosition(currentPos);
            cglib::vec3<double> pos1 = projectionSurface->calculatePosition(prevPos);
            double travel = projectionSurface->calculateDistance(pos0, pos1);
            // What a pixel is worth at the focus - their pixelsPerMeter, which is the map scale
            // and knows nothing about where on the screen the finger is.
            double unitsPerPixel = 2.0 * viewState.calculateCameraDistance() * viewState.getTanHalfFOVY() / std::max(1, viewState.getHeight());
            double limit = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) * unitsPerPixel;
            if (limit > 0 && travel > limit) {
                cglib::mat4x4<double> transform = projectionSurface->calculateTranslateMatrix(pos0, pos1, limit / travel);
                prevPos = projectionSurface->calculateMapPos(cglib::transform_point(pos0, transform));
            }
        }

        CameraPanEvent cameraEvent;
        cameraEvent.setPosDelta(std::make_pair(currentPos, prevPos));
        _cameraEvents |= CAMERA_PAN;
        _mapRenderer->calculateCameraEvent(cameraEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);
    }

    void TouchHandler::singlePointerLook(const ScreenPos& screenPos, const ViewState& viewState) {
        if (_options->isUserInput()) {
            _mapRenderer->getAnimationHandler().stopPan();
            _mapRenderer->getAnimationHandler().stopRotation();
            _mapRenderer->getAnimationHandler().stopTilt();
            _mapRenderer->getAnimationHandler().stopZoom();

            float dpi = _options->getDPI();
            float dx = screenPos.getX() - _prevScreenPos1.getX();
            float dy = screenPos.getY() - _prevScreenPos1.getY();

            // Sideways turns the heading. Dragging left turns the view right, the way dragging the
            // world does, so the gesture reads the same as panning does outside free roam.
            //
            // The turn is about the CAMERA, not about the focus point on the ground: turning your
            // head does not move you. Rotating about the focus - what a map rotation does - swings
            // the camera around a circle of the focus distance, and at a low tilt that walks it
            // straight through the terrain. In first person the rotation event pivots there by
            // itself (the whole camera model does); in LOOK mode it is asked for explicitly.
            if (dx != 0) {
                std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
                CameraRotationEvent cameraEvent;
                cameraEvent.setRotationDelta(dx * _options->getFreeRoamLookSensitivity() / dpi);
                if (projectionSurface && _options->getFreeRoamMode() != FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON) {
                    cameraEvent.setTargetPos(projectionSurface->calculateMapPos(viewState.getCameraPos()));
                }
                _cameraEvents |= CAMERA_ROTATE;
                _mapRenderer->calculateCameraEvent(cameraEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_GESTURE);
            }
            // Up and down changes the tilt, in the same direction the two-finger tilt uses.
            if (dy != 0) {
                float scale = INCHES_TO_TILT_DELTA / dpi;
                if (_options->isTiltGestureReversed()) {
                    scale = -scale;
                }
                CameraTiltEvent cameraEvent;
                cameraEvent.setTiltDelta(dy * scale);
                _cameraEvents |= CAMERA_TILT;
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
            
            // No ground hit required: this zoom is a vertical drag about the FOCUS, and gating it
            // on one killed the gesture wherever the fingers' rays miss - a low camera over
            // terrain, where the ground under half the screen is past the far plane.
            float dpi = _options->getDPI();
            cglib::vec2<float> tempSwipe1(screenPos.getX() - _prevScreenPos1.getX(), screenPos.getY() - _prevScreenPos1.getY());
            _swipe1 += tempSwipe1 * (1.0f / dpi);

            float delta = INCHES_TO_ZOOM_DELTA * (screenPos.getY() - _prevScreenPos1.getY()) / dpi;

            CameraZoomEvent cameraEvent;
            cameraEvent.setZoomDelta(delta);
            _cameraEvents |= CAMERA_ZOOM;
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

            // Calculate swipe vectors
            cglib::vec2<float> tempSwipe1(screenPos1.getX() - _prevScreenPos1.getX(), screenPos1.getY() - _prevScreenPos1.getY());
            _swipe1 += tempSwipe1 * (1.0f / dpi);
            cglib::vec2<float> tempSwipe2(screenPos2.getX() - _prevScreenPos2.getX(), screenPos2.getY() - _prevScreenPos2.getY());
            _swipe2 += tempSwipe2 * (1.0f / dpi);
            
            float swipe1Length = cglib::length(_swipe1);
            float swipe2Length = cglib::length(_swipe2);
    
            // Check if swipes have opposite directions or same directions
            if (((swipe1Length > GUESS_MIN_SWIPE_LENGTH_OPPOSITE_INCHES && prevSwipe1Length > 0) ||
                 (swipe2Length > GUESS_MIN_SWIPE_LENGTH_OPPOSITE_INCHES && prevSwipe2Length > 0))
                && _swipe1(1) * _swipe2(1) <= 0) {
                _gestureMode = DUAL_POINTER_FREE;
            } else if ((swipe1Length > GUESS_MIN_SWIPE_LENGTH_SAME_INCHES ||
                        swipe2Length > GUESS_MIN_SWIPE_LENGTH_SAME_INCHES) 
                       && _swipe1(1) * _swipe2(1) > 0) {
                // Check if the angle of the same direction swipes
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
    
        // The general case requires _previous coordinates for both pointers,
        // calculate them
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
            
            float scale = INCHES_TO_TILT_DELTA / _options->getDPI();
            if (_options->isTiltGestureReversed()) {
                scale = -scale;
            }

            CameraTiltEvent cameraEvent;
            cameraEvent.setTiltDelta((screenPos.getY() - _prevScreenPos1.getY()) * scale);
            _cameraEvents |= CAMERA_TILT;
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

            // First person movement: the two fingers are the movement keys. Dragging them up walks
            // forward, down walks back, sideways strafes - the camera keeps its height, its heading
            // and its zoom, and nothing is anchored to a point on the ground, so this works just as
            // well with the view aimed at the sky, where a map pan has no ground to hold on to.
            float dx = (screenPos1.getX() + screenPos2.getX()) * 0.5f - (_prevScreenPos1.getX() + _prevScreenPos2.getX()) * 0.5f;
            float dy = (screenPos1.getY() + screenPos2.getY()) * 0.5f - (_prevScreenPos1.getY() + _prevScreenPos2.getY()) * 0.5f;
            _prevScreenPos1 = screenPos1;
            _prevScreenPos2 = screenPos2;
            if (dx == 0 && dy == 0) {
                return;
            }

            // The horizontal frame the movement happens in, taken from the view itself: forward is
            // where the camera looks, flattened onto the ground.
            cglib::vec3<double> cameraPos = viewState.getCameraPos();
            MapPos cameraMapPos = projectionSurface->calculateMapPos(cameraPos);
            cglib::vec3<double> normal = projectionSurface->calculateNormal(cameraMapPos);
            cglib::vec3<double> viewDir = viewState.calculateViewDir();
            // Threshold, not '== 0' - see singlePointerPan: a view that is vertical only to within
            // rounding leaves a ~1e-16 cross product, and normalising that is normalising noise.
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

            // Distance per inch of drag, as a fraction of the camera to focus distance, so a move
            // covers the same part of the view at any zoom.
            double perInch = _options->getFreeRoamMoveSpeed() * viewState.calculateCameraDistance();
            double dpi = _options->getDPI();
            cglib::vec3<double> offset = forward * (-dy / dpi * perInch) + right * (-dx / dpi * perInch);

            CameraPanEvent cameraEvent;
            cameraEvent.setPosDelta(std::make_pair(cameraMapPos, projectionSurface->calculateMapPos(cameraPos + offset)));
            _cameraEvents |= CAMERA_PAN;
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

            // The scale and the angle are what the FINGERS did, taken from the SCREEN - which is
            // where a pinch and a two-finger turn happen, and how tangram takes them (inputHandler
            // handlePinchGesture/handleRotateGesture, fed by the platform's gesture detector).
            // Deriving them from where the two rays meet the ground instead - what this did - hands
            // a grazing ray straight to the camera: a low camera over terrain puts one finger's hit
            // kilometres away, so a pinch of a few pixels comes out as a wild zoom or spin, and
            // where the ray missed the ground altogether the whole gesture was dropped and the map
            // could not be zoomed at all.
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
                _cameraEvents |= CAMERA_ZOOM;
                _mapRenderer->calculateCameraEvent(cameraZoomTargetEvent, 0, true, MapMoveReason::MAP_MOVE_REASON_GESTURE);
            }

            if (rotate && _options->isRotationGestures() && prevDist > 0 && currentDist > 0) {
                // Signed angle from the previous finger vector to the current one. Screen y points
                // down, which is what turns a clockwise turn of the fingers into a positive map
                // rotation - the same sign the ground-derived cross product produced.
                double cross = static_cast<double>(prevVec(0)) * currentVec(1) - static_cast<double>(prevVec(1)) * currentVec(0);
                double dot = static_cast<double>(prevVec(0)) * currentVec(0) + static_cast<double>(prevVec(1)) * currentVec(1);
                CameraRotationEvent cameraRotateTargetEvent;
                cameraRotateTargetEvent.setRotationDelta(static_cast<float>(std::atan2(cross, dot) * Const::RAD_TO_DEG));
                cameraRotateTargetEvent.setTargetPos(pivotPos);
                _cameraEvents |= CAMERA_ROTATE;
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
        // The plane the gesture is actually anchored to (mapScreenPosition uses the same one).
        // Testing the SEA LEVEL plane instead reported a touch as valid, or as past the far plane,
        // for a surface no gesture ever uses - in the mountains the two are hundreds of metres and,
        // at a low tilt, kilometres of ray apart.
        cglib::vec3<double> pos = viewState.screenToWorld(cglib::vec2<float>(screenPos.getX(), screenPos.getY()), _gestureAnchorHeight.load());
        if (std::isnan(cglib::norm(pos))) {
            return false;
        }
        cglib::vec3<double> zVec = cglib::unit(viewState.getFocusPos() - viewState.getCameraPos());
        double dist = cglib::dot_product(zVec, pos - viewState.getCameraPos());
        return dist > 0 && dist < viewState.getFar();
    }

    cglib::ray3<double> TouchHandler::calculateScreenRay(const ScreenPos& screenPos, const ViewState& viewState) const {
        // The same unprojection ViewState::screenToWorld does, stopping at the ray: two points at
        // the near and far planes, which is a direction whether or not it ever meets the ground.
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

    /**
     * How much map a screen pixel is worth where the gesture starts, which is what fixes the pan
     * speed for the rest of it. On a tilted view this varies over the screen by orders of
     * magnitude - that is the whole point of measuring it once.
     */
    double TouchHandler::calculatePanScale(const ScreenPos& screenPos, const ViewState& viewState) const {
        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface || viewState.getHeight() <= 0) {
            return 0;
        }
        // A pixel at the FAR plane is the largest a pixel can honestly be worth: near the horizon
        // the two sample rays run almost parallel to the ground and their hit points fly apart.
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
            // No ground under the touch - the view is aimed at the sky, or past the horizon. The
            // centre of the screen is the fallback, and the far plane the last resort.
            samplePos = ScreenPos(viewState.getHalfWidth(), viewState.getHalfHeight());
        }
        return maxScale;
    }

    MapPos TouchHandler::mapScreenPosition(const ScreenPos& screenPos, const ViewState& viewState) const {
        cglib::vec3<double> pos = viewState.screenToWorld(cglib::vec2<float>(screenPos.getX(), screenPos.getY()), _gestureAnchorHeight.load());
        return viewState.getProjectionSurface()->calculateMapPos(pos);
    }

    /**
     * The point a zoom or a rotation turns about: what is under the fingers when the map is
     * there, the focus otherwise. A missing ground hit must NOT cancel the gesture - that is what
     * left the map frozen with the camera close to the terrain, where half the screen is sky or
     * ground past the far plane.
     */
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
        cglib::ray3<double> ray(viewState.getCameraPos(), worldPos - viewState.getCameraPos());
        double t = 0;
        if (terrainOptions->getElevationManager()->intersectRay(ray, t) && t > 0) {
            return ray(t)(2);
        }
        return 0;
    }

    void TouchHandler::updateGestureAnchorHeight(const ScreenPos& screenPos, const ViewState& viewState) {
        _gestureAnchorHeight.store(calculateTerrainHeight(screenPos, viewState));
    }

    void TouchHandler::handleClick(const ClickInfo& clickInfo, const ScreenPos& screenPos) {
        ViewState viewState = _mapRenderer->getViewState();
        // A touch aimed at the SKY has no ground position - the ray never meets the plane - but it
        // is still a ray, and layers anchored in the sky (CelestialLayer) live along it. Ask the
        // layers with the ray alone in that case; there is no map position to report afterwards.
        bool groundHit = isValidScreenPosition(screenPos, viewState);
        std::vector<RayIntersectedElement> results;
        MapPos mapPos;
        if (groundHit) {
            updateGestureAnchorHeight(screenPos, viewState);
            mapPos = mapScreenPosition(screenPos, viewState);
            _mapRenderer->calculateRayIntersectedElements(mapPos, viewState, results);
        } else {
            cglib::ray3<double> ray = calculateScreenRay(screenPos, viewState);
            if (std::isnan(cglib::norm(ray.direction))) {
                return;
            }
            _mapRenderer->calculateRayIntersectedElements(ray, viewState, results);
        }
    
        // Sort the results but do 'reverse stable sort' to be consistent with the rendering order
        std::stable_sort(results.begin(), results.end(), RayIntersectedElementComparator(viewState));
        std::reverse(results.begin(), results.end());

        // Process the results
        for (const RayIntersectedElement& intersectedElement : results) {
            if (intersectedElement.getLayer()->processClick(clickInfo, intersectedElement, viewState)) {
                return;
            }
        }

        // Click was ignored by layers, call map event listener. Nothing to report for a touch
        // that never reached the ground.
        if (!groundHit) {
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
        // First person: two fingers MOVE, and there is nothing to guess between - a pinch and a
        // two-finger rotation are map gestures, and this control scheme has neither.
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
            {
                std::lock_guard<std::recursive_mutex> lock(touchHandler->_mutex);
                touchHandler->_idling = false;
                if (touchHandler->_cameraEvents) {
                    return; // postpone listener call, will be called together with onMapInteraction
                }
            }

            DirectorPtr<MapEventListener> mapEventListener = touchHandler->_mapEventListener;

            if (mapEventListener) {
                mapEventListener->onMapMoved(reason);
            }
        }
    }
    
    void TouchHandler::MapRendererListener::onMapIdle() {
        if (auto touchHandler = _touchHandler.lock()) {
            {
                std::lock_guard<std::recursive_mutex> lock(touchHandler->_mutex);
                touchHandler->_idling = true;
            }

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
    // A full turn takes about two swipes across a phone, which is what a look control wants.

    const float TouchHandler::INCHES_TO_ZOOM_DELTA = 1.0f;

    const float TouchHandler::PAN_CLAMP_MAX_TILT = 15.0f; // tangram's pitch > 75 degrees
        
    const std::chrono::milliseconds TouchHandler::DUAL_KINETIC_HOLD_DURATION = std::chrono::milliseconds(100);

    const std::chrono::milliseconds TouchHandler::DUAL_STOP_HOLD_DURATION = std::chrono::milliseconds(75);

    const std::chrono::milliseconds TouchHandler::ZOOM_GESTURE_ANIMATION_DURATION = std::chrono::milliseconds(250);

}

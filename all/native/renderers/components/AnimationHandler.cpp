#include "AnimationHandler.h"
#include "projections/ProjectionSurface.h"
#include "renderers/MapRenderer.h"
#include "core/MapPos.h"
#include "graphics/ViewState.h"
#include "utils/Const.h"
#include "utils/UnitBezier.h"

#include <cmath>

namespace massif {

    namespace {

        // Van Wijk's V, in screenfuls per second. Their optimum, and mapbox-gl's `speed` default.
        const double FLIGHT_SPEED = 1.2;

        double applyEasing(FlightEasing::FlightEasing easing, double t) {
            switch (easing) {
            case FlightEasing::FLIGHT_EASING_LINEAR:
                return t;
            case FlightEasing::FLIGHT_EASING_EASE_IN:
                return UnitBezier(0.42, 0.0, 1.0, 1.0).solve(t);
            case FlightEasing::FLIGHT_EASING_EASE_OUT:
                return UnitBezier(0.0, 0.0, 0.58, 1.0).solve(t);
            case FlightEasing::FLIGHT_EASING_EASE_IN_OUT:
                return UnitBezier(0.42, 0.0, 0.58, 1.0).solve(t);
            default:
                return UnitBezier(0.25, 0.1, 0.25, 1.0).solve(t);
            }
        }

    }

    AnimationHandler::AnimationHandler(MapRenderer& mapRenderer) :
        _panStarted(false),
        _panDurationSeconds(0),
        _panTarget(),
        _panDelta(),
        _panUseDelta(false),
        _rotationStarted(false),
        _rotationDurationSeconds(0),
        _rotationTarget(),
        _rotationTargetPos(),
        _tiltStarted(false),
        _tiltDurationSeconds(0),
        _tiltTarget(),
        _zoomStarted(false),
        _zoomDurationSeconds(0),
        _zoomTarget(),
        _zoomTargetPos(),
        _flightActive(false),
        _flightStarted(false),
        _flightElapsed(0),
        _flightDuration(0),
        _flightRho(1.42),
        _flightEasing(FlightEasing::FLIGHT_EASING_EASE),
        _flightPath(),
        _flightStartPos(),
        _flightTargetPos(),
        _flightClimb(0),
        _flightProgress(-1),
        _flightStartZoom(0),
        _flightTargetZoom(0),
        _flightStartRotation(),
        _flightTargetRotation(),
        _flightStartTilt(),
        _flightTargetTilt(),
        _mapRenderer(mapRenderer),
        _mutex()
    {
    }
    
    AnimationHandler::~AnimationHandler() {
    }
    
    void AnimationHandler::calculate(const ViewState& viewState, float deltaSeconds) {
        std::optional<CameraPanEvent> cameraPanEvent;
        std::optional<CameraRotationEvent> cameraRotationEvent;
        std::optional<CameraTiltEvent> cameraTiltEvent;
        std::optional<CameraZoomEvent> cameraZoomEvent;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_flightActive) {
                // One clock for the whole move: the per-property animations are not running while
                // a flight is (setFlightTarget stops them), so nothing fights it for the camera.
                calculateFlight(viewState, deltaSeconds, cameraPanEvent, cameraRotationEvent, cameraTiltEvent, cameraZoomEvent);
            } else {
                cameraPanEvent = calculatePan(viewState, deltaSeconds);
                cameraRotationEvent = calculateRotation(viewState, deltaSeconds);
                cameraTiltEvent = calculateTilt(viewState, deltaSeconds);
                cameraZoomEvent = calculateZoom(viewState, deltaSeconds);
            }
        }
        if (cameraPanEvent) {
            _mapRenderer.calculateCameraEvent(*cameraPanEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_ANIMATION);
        }
        if (cameraRotationEvent) {
            _mapRenderer.calculateCameraEvent(*cameraRotationEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_ANIMATION);
        }
        if (cameraTiltEvent) {
            _mapRenderer.calculateCameraEvent(*cameraTiltEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_ANIMATION);
        }
        if (cameraZoomEvent) {
            _mapRenderer.calculateCameraEvent(*cameraZoomEvent, 0, false, MapMoveReason::MAP_MOVE_REASON_ANIMATION);
        }

        // An animation that is not finished owes itself the next frame, as KineticEventHandler does.
        // Without this it advanced only as far as something ELSE redrew, so a move over a map with
        // no layers yet stopped where it was - a flight never left its starting camera at all.
        if (isAnimating()) {
            _mapRenderer.requestRedraw();
        }
    }

    bool AnimationHandler::isAnimating() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _flightActive || _panDurationSeconds > 0 || _rotationDurationSeconds > 0 ||
               _tiltDurationSeconds > 0 || _zoomDurationSeconds > 0;
    }
    
    void AnimationHandler::setPanTarget(const MapPos& panTarget, float durationSeconds) {
        std::lock_guard<std::mutex> lock(_mutex);
        _panStarted = true;
        _panTarget = panTarget;
        _panDurationSeconds = durationSeconds;
        _panUseDelta = false;
    }
    
    void AnimationHandler::setPanDelta(const std::pair<MapPos, MapPos>& panDelta, float durationSeconds) {
        std::lock_guard<std::mutex> lock(_mutex);
        _panStarted = true;
        _panDelta = panDelta;
        _panDurationSeconds = durationSeconds;
        _panUseDelta = true;
    }

    void AnimationHandler::stopPan() {
        std::lock_guard<std::mutex> lock(_mutex);
        _panDurationSeconds = 0;
    }
    
    void AnimationHandler::setRotationTarget(float rotationTarget, const MapPos* targetPos, float durationSeconds) {
        std::lock_guard<std::mutex> lock(_mutex);
        _rotationStarted = true;
        _rotationTarget = std::fmod(rotationTarget, 360.0f);
        if (_rotationTarget > 180) {
            _rotationTarget -= 360;
        } else if (_rotationTarget < -180) {
            _rotationTarget += 360;
        }
        _rotationTargetPos.reset();
        if (targetPos) {
            _rotationTargetPos = *targetPos;
        }
        _rotationDurationSeconds = durationSeconds;
    }
        
    void AnimationHandler::stopRotation() {
        std::lock_guard<std::mutex> lock(_mutex);
        _rotationDurationSeconds = 0;
    }
    
    void AnimationHandler::setTiltTarget(float tiltTarget, float durationSeconds) {
        std::lock_guard<std::mutex> lock(_mutex);
        _tiltStarted = true;
        _tiltTarget = tiltTarget;
        _tiltDurationSeconds = durationSeconds;
    }
        
    void AnimationHandler::stopTilt() {
        std::lock_guard<std::mutex> lock(_mutex);
        _tiltDurationSeconds = 0;
    }
    
    void AnimationHandler::setZoomTarget(float zoomTarget, const MapPos* targetPos, float durationSeconds) {
        std::lock_guard<std::mutex> lock(_mutex);
        _zoomStarted = true;
        _zoomTarget = zoomTarget;
        _zoomTargetPos.reset();
        if (targetPos) {
            _zoomTargetPos = *targetPos;
        }
        _zoomDurationSeconds = durationSeconds;
    }
        
    void AnimationHandler::stopZoom() {
        std::lock_guard<std::mutex> lock(_mutex);
        _zoomDurationSeconds = 0;
    }
    
    void AnimationHandler::setFlightTarget(const MapPos& pos, float zoom, const float* rotation, const float* tilt, float climbHeight, float durationSeconds, float rho, FlightEasing::FlightEasing easing) {
        std::lock_guard<std::mutex> lock(_mutex);

        // The per-property animations would keep pulling the camera their own way.
        _panDurationSeconds = 0;
        _rotationDurationSeconds = 0;
        _tiltDurationSeconds = 0;
        _zoomDurationSeconds = 0;

        _flightRho = (rho > 0.1f ? rho : 1.42);
        _flightEasing = easing;
        _flightTargetPos = pos;
        _flightTargetZoom = zoom;
        _flightStartRotation.reset();
        _flightTargetRotation.reset();
        if (rotation) {
            _flightTargetRotation = *rotation;
        }
        _flightStartTilt.reset();
        _flightTargetTilt.reset();
        if (tilt) {
            _flightTargetTilt = *tilt;
        }
        _flightClimb = climbHeight;
        _flightDuration = durationSeconds;
        _flightElapsed = 0;
        _flightProgress = 0;
        _flightActive = true;
        _flightStarted = true; // the path is set up on the first frame, where the view state is known
    }

    void AnimationHandler::stopFlight() {
        std::lock_guard<std::mutex> lock(_mutex);
        _flightActive = false;
    }

    bool AnimationHandler::isFlightActive() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _flightActive;
    }

    float AnimationHandler::getFlightProgress() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _flightProgress;
    }

    void AnimationHandler::calculateFlight(const ViewState& viewState, float deltaSeconds, std::optional<CameraPanEvent>& panEvent, std::optional<CameraRotationEvent>& rotationEvent, std::optional<CameraTiltEvent>& tiltEvent, std::optional<CameraZoomEvent>& zoomEvent) {
        std::shared_ptr<ProjectionSurface> projectionSurface = _mapRenderer.getProjectionSurface();
        if (!projectionSurface) {
            _flightActive = false;
            return;
        }

        if (_flightStarted) {
            // Set the path up against the view the flight actually starts from. The first delta is
            // discarded for the same reason the other animations discard theirs: on-demand
            // rendering can hand the first frame an arbitrarily large one.
            _flightStarted = false;
            deltaSeconds = 0;
            _flightStartPos = projectionSurface->calculateMapPos(viewState.getFocusPos());
            _flightStartZoom = viewState.getZoom();
            _flightStartRotation = viewState.getRotation();
            _flightStartTilt = viewState.getTilt();

            // Van Wijk's w is a SCREENFUL: measuring it against a tile pulled every flight back by
            // a constant log2(screen / tile). See docs/internals/rendering/01-frame.md.
            double spanPerZoom = viewState.getSpanPerZoom();
            if (!(spanPerZoom > 0)) {
                // No viewport yet: WORLD_SIZE still gives the right w1/w0, only a wrong arc depth.
                spanPerZoom = Const::WORLD_SIZE;
            }
            double w0 = spanPerZoom * std::pow(2.0, -static_cast<double>(_flightStartZoom));
            double w1 = spanPerZoom * std::pow(2.0, -static_cast<double>(_flightTargetZoom));
            // Ground only: folded into u, a pure climb flew an arc across a map it never crosses.
            cglib::vec3<double> p0 = projectionSurface->calculatePosition(MapPos(_flightStartPos.getX(), _flightStartPos.getY(), 0));
            cglib::vec3<double> p1 = projectionSurface->calculatePosition(MapPos(_flightTargetPos.getX(), _flightTargetPos.getY(), 0));
            _flightPath.setup(w0, w1, cglib::length(p1 - p0), _flightRho);

            if (!(_flightDuration > 0)) {
                // Their point: the duration follows the length of the path, so a move twice as far
                // does not take twice as long. V is in their units of "screenfuls per second".
                _flightDuration = static_cast<float>(_flightPath.suggestedDuration(FLIGHT_SPEED, 0.25));
            }
        }

        _flightElapsed += deltaSeconds;
        float t = (_flightDuration > 0 ? std::min(1.0f, _flightElapsed / _flightDuration) : 1.0f);
        bool done = (t >= 1.0f);
        // ONE eased clock for the whole move, as mapbox-gl runs theirs.
        double k = (done ? 1.0 : applyEasing(_flightEasing, t));

        double ratio = 1.0;
        double zoom = _flightTargetZoom;
        if (!done) {
            double zoomDelta = 0;
            _flightPath.sample(k, ratio, zoomDelta);
            zoom = _flightStartZoom + zoomDelta;
        }

        MapPos newFocusPos = _flightTargetPos;
        if (!done) {
            cglib::vec3<double> pos0 = projectionSurface->calculatePosition(_flightStartPos);
            cglib::vec3<double> pos1 = projectionSurface->calculatePosition(_flightTargetPos);
            cglib::mat4x4<double> transform = projectionSurface->calculateTranslateMatrix(pos0, pos1, ratio);
            newFocusPos = projectionSurface->calculateMapPos(cglib::transform_point(pos0, transform));
        }
        double height = _flightStartPos.getZ() + (_flightTargetPos.getZ() - _flightStartPos.getZ()) * k;
        if (!done && _flightClimb != 0) {
            height += _flightClimb * 4.0 * k * (1.0 - k);
        }
        newFocusPos.setZ(height);
        _flightProgress = static_cast<float>(k);

        CameraPanEvent panCameraEvent;
        panCameraEvent.setKeepRotation(true);
        panCameraEvent.setPos(newFocusPos);
        panEvent = panCameraEvent;

        CameraZoomEvent zoomCameraEvent;
        zoomCameraEvent.setZoom(static_cast<float>(zoom));
        zoomEvent = zoomCameraEvent;

        if (_flightTargetRotation && _flightStartRotation) {
            float delta = *_flightTargetRotation - *_flightStartRotation;
            if (delta > 180) {
                delta -= 360;
            } else if (delta < -180) {
                delta += 360;
            }
            CameraRotationEvent rotationCameraEvent;
            rotationCameraEvent.setRotation(*_flightStartRotation + static_cast<float>(delta * k));
            rotationEvent = rotationCameraEvent;
        }
        if (_flightTargetTilt && _flightStartTilt) {
            CameraTiltEvent tiltCameraEvent;
            tiltCameraEvent.setTilt(*_flightStartTilt + static_cast<float>((*_flightTargetTilt - *_flightStartTilt) * k));
            tiltEvent = tiltCameraEvent;
        }

        if (done) {
            _flightActive = false;
            _flightProgress = -1;
        }
    }

    std::optional<CameraPanEvent> AnimationHandler::calculatePan(const ViewState& viewState, float deltaSeconds) {
        // Disregard the first calculation event, because the deltaSeconds parameter may be
        // very large. It's caused by on-demand rendering.
        if (_panStarted) {
            _panStarted = false;
            deltaSeconds = 0;
        }
        if (_panDurationSeconds > 0) {
            float ratio = std::min(1.0f, deltaSeconds / _panDurationSeconds);
            _panDurationSeconds -= deltaSeconds;
            MapPos newFocusPos;
            if (_panDurationSeconds <= 0 && !_panUseDelta) {
                newFocusPos = _panTarget;
            } else {
                std::shared_ptr<ProjectionSurface> projectionSurface = _mapRenderer.getProjectionSurface();
                cglib::vec3<double> pos0 = _panUseDelta ? projectionSurface->calculatePosition(_panDelta.first) : viewState.getFocusPos();
                cglib::vec3<double> pos1 = _panUseDelta ? projectionSurface->calculatePosition(_panDelta.second) : projectionSurface->calculatePosition(_panTarget);
                cglib::mat4x4<double> transform = projectionSurface->calculateTranslateMatrix(pos0, pos1, ratio);
                newFocusPos = projectionSurface->calculateMapPos(cglib::transform_point(viewState.getFocusPos(), transform));
            }
    
            CameraPanEvent cameraEvent;
            cameraEvent.setPos(newFocusPos);
            return cameraEvent;
        }
        return std::optional<CameraPanEvent>();
    }
    
    std::optional<CameraRotationEvent> AnimationHandler::calculateRotation(const ViewState& viewState, float deltaSeconds) {
        // Disregard the first calculation event, because the deltaSeconds parameter may be
        // very large. It's caused by on-demand rendering.
        if (_rotationStarted) {
            _rotationStarted = false;
            deltaSeconds = 0;
        }
        if (_rotationDurationSeconds > 0) {
            float ratio = std::min(1.0f, deltaSeconds / _rotationDurationSeconds);
            _rotationDurationSeconds -= deltaSeconds;
            float newRotation;
            if (_rotationDurationSeconds <= 0) {
               newRotation = _rotationTarget;
            } else {
                float delta = _rotationTarget - viewState.getRotation();
                if (delta > 180) {
                    delta -= 360;
                } else if (delta < -180) {
                    delta += 360;
                }
                delta *= ratio;
                newRotation = viewState.getRotation() + delta;
            }
        
            CameraRotationEvent cameraEvent;
            cameraEvent.setRotation(newRotation);
            if (_rotationTargetPos) {
                cameraEvent.setTargetPos(*_rotationTargetPos);
            }
            return cameraEvent;
        }
        return std::optional<CameraRotationEvent>();
    }
    
    std::optional<CameraTiltEvent> AnimationHandler::calculateTilt(const ViewState& viewState, float deltaSeconds) {
        // Disregard the first calculation event, because the deltaSeconds parameter may be
        // very large. It's caused by on-demand rendering.
        if (_tiltStarted) {
            _tiltStarted = false;
            deltaSeconds = 0;
        }
        if (_tiltDurationSeconds > 0) {
            float ratio = std::min(1.0f, deltaSeconds / _tiltDurationSeconds);
            _tiltDurationSeconds -= deltaSeconds;
            float newTilt;
            if (_tiltDurationSeconds <= 0) {
                newTilt = _tiltTarget;
            } else {
                float delta = _tiltTarget - viewState.getTilt();
                delta *= ratio;
                newTilt = viewState.getTilt() + delta;
            }
    
            CameraTiltEvent cameraEvent;
            cameraEvent.setTilt(newTilt);
            return cameraEvent;
        }
        return std::optional<CameraTiltEvent>();
    }
    
    std::optional<CameraZoomEvent> AnimationHandler::calculateZoom(const ViewState& viewState, float deltaSeconds) {
        // Disregard the first calculation event, because the deltaSeconds parameter may be
        // very large. It's caused by on-demand rendering.
        if (_zoomStarted) {
            _zoomStarted = false;
            deltaSeconds = 0;
        }
        if (_zoomDurationSeconds > 0) {
            float ratio = std::min(1.0f, deltaSeconds / _zoomDurationSeconds);
            _zoomDurationSeconds -= deltaSeconds;
            float newZoom;
            if (_zoomDurationSeconds <= 0) {
                newZoom = _zoomTarget;
            } else {
                float delta = _zoomTarget - viewState.getZoom();
                delta *= ratio;
                newZoom = viewState.getZoom() + delta;
            }
    
            CameraZoomEvent cameraEvent;
            cameraEvent.setZoom(newZoom);
            if (_zoomTargetPos) {
                cameraEvent.setTargetPos(*_zoomTargetPos);
            }
            return cameraEvent;
        }
        return std::optional<CameraZoomEvent>();
    }
    
}

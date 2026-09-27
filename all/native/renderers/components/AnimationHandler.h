/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ANIMATIONHANDLER_H_
#define _MASSIF_ANIMATIONHANDLER_H_

#include "core/MapPos.h"
#include "renderers/cameraevents/CameraPanEvent.h"
#include "renderers/cameraevents/CameraRotationEvent.h"
#include "renderers/cameraevents/CameraTiltEvent.h"
#include "renderers/cameraevents/CameraZoomEvent.h"
#include "renderers/components/FlightPath.h"
#include "ui/FlightEasing.h"

#include <optional>
#include <memory>
#include <mutex>

namespace massif {
    class MapRenderer;
    class ViewState;
    
    class AnimationHandler {
    public:
        explicit AnimationHandler(MapRenderer& mapRenderer);
        virtual ~AnimationHandler();
    
        void calculate(const ViewState& viewState, float deltaSeconds);
    
        void setPanTarget(const MapPos& panTarget, float durationSeconds);
        void setPanDelta(const std::pair<MapPos, MapPos>& panDelta, float durationSeconds);
        void stopPan();
        
        void setRotationTarget(float rotationTarget, const MapPos* targetPos, float durationSeconds);
        void stopRotation();
        
        void setTiltTarget(float tiltTarget, float durationSeconds);
        void stopTilt();
        
        void setZoomTarget(float zoomTarget, const MapPos* targetPos, float durationSeconds);
        void stopZoom();

        /**
         * One camera move along Van Wijk & Nuij's optimal zoom/pan path (2003); positions in internal units.
         * durationSeconds <= 0 derives it from the path length; rho is the pull-back (1.42 optimal).
         * easing is the timing curve. Supersedes the per-property targets while it runs.
         */
        void setFlightTarget(const MapPos& pos, float zoom, const float* rotation, const float* tilt, float climbHeight, float durationSeconds, float rho, FlightEasing::FlightEasing easing);
        void stopFlight();
        bool isFlightActive() const;
        /**
         * Eased flight progress, 0 to 1, or -1 when none is running.
         */
        float getFlightProgress() const;
        /**
         * The flight's climb at this frame, internal units: added over the ground rule's focus height,
         * which would overwrite a climb written into the focus itself. 0 when no flight runs.
         */
        double getFlightLift() const;

        /**
         * Whether a flight or any per-property animation is still moving the camera.
         */
        bool isAnimating() const;

    private:
        void calculateFlight(const ViewState& viewState, float deltaSeconds, std::optional<CameraPanEvent>& panEvent, std::optional<CameraRotationEvent>& rotationEvent, std::optional<CameraTiltEvent>& tiltEvent, std::optional<CameraZoomEvent>& zoomEvent);
        std::optional<CameraPanEvent> calculatePan(const ViewState& viewState, float deltaSeconds);
        std::optional<CameraRotationEvent> calculateRotation(const ViewState& viewState, float deltaSeconds);
        std::optional<CameraTiltEvent> calculateTilt(const ViewState& viewState, float deltaSeconds);
        std::optional<CameraZoomEvent> calculateZoom(const ViewState& viewState, float deltaSeconds);
    
        bool _panStarted;
        float _panDurationSeconds;
        MapPos _panTarget;
        std::pair<MapPos, MapPos> _panDelta;
        bool _panUseDelta;
    
        bool _rotationStarted;
        float _rotationDurationSeconds;
        float _rotationTarget;
        std::optional<MapPos> _rotationTargetPos;
    
        bool _tiltStarted;
        float _tiltDurationSeconds;
        float _tiltTarget;
    
        bool _zoomStarted;
        float _zoomDurationSeconds;
        float _zoomTarget;
        std::optional<MapPos> _zoomTargetPos;
    
        bool _flightActive;
        bool _flightStarted;
        float _flightElapsed;
        float _flightDuration;
        double _flightRho;
        FlightEasing::FlightEasing _flightEasing;
        FlightPath _flightPath;
        MapPos _flightStartPos;
        MapPos _flightTargetPos;
        double _flightClimb; // internal units added at the middle of the path, parabolic
        double _flightLift; // the climb at the current frame, see getFlightLift
        bool _flightFirstPerson; // eye-led path: no van Wijk zoom-out, which backs a first-person eye away
        bool _firstPersonHint; // the free roam mode at this frame, read before _mutex
        float _flightProgress;
        float _flightStartZoom;
        float _flightTargetZoom;
        std::optional<float> _flightStartRotation;
        std::optional<float> _flightTargetRotation;
        std::optional<float> _flightStartTilt;
        std::optional<float> _flightTargetTilt;

        MapRenderer& _mapRenderer;
    
        mutable std::mutex _mutex;
    };
    
}

#endif

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
         * One camera move for pan, zoom and (optionally) rotation and tilt, on a single clock:
         * the zoom pulls back over a long move and comes down at the target, along Van Wijk &
         * Nuij's optimal path ("Smooth and efficient zooming and panning", 2003). Positions are
         * in INTERNAL coordinates. durationSeconds <= 0 derives the duration from the path
         * length, which is the point of their parametrisation - a move twice as far does not take
         * twice as long. rho is the aggressiveness of the pull-back (their rho, 1.42 is the value
         * they derive as optimal). Supersedes the per-property targets while it runs.
         */
        void setFlightTarget(const MapPos& pos, float zoom, const float* rotation, const float* tilt, float climbHeight, float durationSeconds, float rho);
        void stopFlight();
        bool isFlightActive() const;
        /**
         * How far along the flight is, 0 to 1, or -1 when none is running. It is the EASED value
         * the camera is at, so an app animating its own state alongside the move can read it
         * instead of re-implementing the easing.
         */
        float getFlightProgress() const;

        /**
         * Whether anything is still moving the camera - a flight or any per-property animation.
         * calculate() uses it to owe itself the next frame; nothing runs a clock of its own.
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
    
        // Van Wijk flight state. _flightS is the total path length in their units and _flightU1
        // the ground distance; the rest is the precomputed parametrisation (their r0, and the
        // widths at both ends). _flightZeroPath marks the degenerate case of a pure zoom, where
        // their formula divides by the distance.
        bool _flightActive;
        bool _flightStarted;
        bool _flightZeroPath;
        float _flightElapsed;
        float _flightDuration;
        double _flightRho;
        double _flightU1;
        double _flightW0;
        double _flightW1;
        double _flightR0;
        double _flightS;
        MapPos _flightStartPos;
        MapPos _flightTargetPos;
        double _flightClimb; // internal units added at the middle of the path, parabolic
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

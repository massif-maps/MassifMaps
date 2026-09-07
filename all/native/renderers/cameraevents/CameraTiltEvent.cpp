#include "CameraTiltEvent.h"
#include "CameraRotationEvent.h"
#include "components/Options.h"
#include "graphics/ViewState.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "utils/Const.h"
#include "utils/Log.h"
#include "utils/GeneralUtils.h"

#include <algorithm>
#include <cmath>

namespace massif {

    CameraTiltEvent::CameraTiltEvent() :
        _keepRotation(false),
        _tilt(0),
        _tiltDelta(0),
        _useDelta(true)
    {
    }
    
    CameraTiltEvent::~CameraTiltEvent() {
    }
    
    bool CameraTiltEvent::isKeepRotation() const {
        return _keepRotation;
    }

    void CameraTiltEvent::setKeepRotation(bool keepRotation) {
        _keepRotation = keepRotation;
    }

    float CameraTiltEvent::getTilt() const {
        return _tilt;
    }
    
    void CameraTiltEvent::setTilt(float tilt) {
        _tilt = tilt;
        _useDelta = false;
    }
    
    float CameraTiltEvent::getTiltDelta() const {
        return _tiltDelta;
    }
    
    void CameraTiltEvent::setTiltDelta(float tiltDelta) {
        _tiltDelta = tiltDelta;
        _useDelta = true;
    }
    
    bool CameraTiltEvent::isUseDelta() const {
        return _useDelta;
    }
    
    void CameraTiltEvent::calculate(Options& options, ViewState& viewState) {
        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface) {
            return;
        }
        
        float rotation = viewState.getRotation();
        cglib::vec3<double> cameraPos = viewState.getCameraPos();
        cglib::vec3<double> focusPos = viewState.getFocusPos();
        cglib::vec3<double> upVec = viewState.getUpVec();
    
        if (_useDelta) {
            // Calculate absolute tilt
            _tilt = viewState.getTilt() + _tiltDelta;
        }
    
        // Enforce tilt range
        MapRange tiltRange = options.getTiltRange();
        float tilt = GeneralUtils::Clamp(_tilt, tiltRange.getMin(), tiltRange.getMax());
    
        // Calculate camera parameters
        cglib::vec3<double> normal = projectionSurface->calculateNormal(projectionSurface->calculateMapPos(focusPos));
        cglib::vec3<double> axis = cglib::vector_product(normal, upVec);
        if (cglib::length(axis) == 0) {
            return;
        }
        
        if (options.getFreeRoamMode() == FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON) {
            // First person: the camera does not move at all, the view rotates about it
            // (ViewState::calculateLookatMat). An orientation-driven camera needs this too, so
            // setTilt from a sensor behaves exactly like the drag.
            viewState.setViewTilt(tilt);
            viewState.cameraChanged();
            if (_keepRotation) {
                CameraRotationEvent cameraRotationEvent;
                cameraRotationEvent.setRotation(rotation);
                cameraRotationEvent.calculate(options, viewState);
            }
            return;
        }

        // Only the part of the tilt at or above the horizon moves the camera: a negative tilt is a
        // look UP from where the camera already is, and rotating it on under the ground instead
        // flips the view over.
        float groundTilt = std::max(tilt, 0.0f);
        cglib::mat4x4<double> tiltTransform = cglib::rotate4_matrix(axis, (groundTilt - viewState.getCameraTilt()) * Const::DEG_TO_RAD);
        cameraPos = focusPos + cglib::transform_vector(cameraPos - focusPos, tiltTransform);
        upVec = cglib::transform_vector(upVec, tiltTransform);
    
        viewState.setCameraPos(cameraPos);
        viewState.setUpVec(upVec);
        viewState.setTilt(tilt);

        viewState.clampFocusPos(options);
        
        // Calculate matrices etc. on the next onDrawFrame() call
        viewState.cameraChanged();

        // Restore rotation, if needed
        if (_keepRotation) {
            CameraRotationEvent cameraRotationEvent;
            cameraRotationEvent.setRotation(rotation);
            cameraRotationEvent.calculate(options, viewState);
        }
    }
    
}

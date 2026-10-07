/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CAMERAZOOMEVENT_H_
#define _MASSIF_CAMERAZOOMEVENT_H_

#include "core/MapPos.h"
#include "renderers/cameraevents/CameraEvent.h"

namespace massif {

    class CameraZoomEvent : public CameraEvent {
    public:
        CameraZoomEvent();
        virtual ~CameraZoomEvent();
    
        bool isKeepRotation() const;
        void setKeepRotation(bool keepRotation);
    
        float getZoom() const;
        void setZoom(float zoom);
    
        float getZoomDelta() const;
        void setZoomDelta(float zoomDelta);
        void setScale(float scale);
        
        const MapPos& getTargetPos() const;
        void setTargetPos(const MapPos& targetPos);
        // The target is a ground point at its own height and stays where it is on screen (a gesture's pivot);
        // otherwise it is taken at the focus height.
        void setPinTarget(bool pinTarget);
    
        bool isUseDelta() const;
        bool isUseTarget() const;
    
        void calculate(Options& options, ViewState& viewState);

    private:
        bool _keepRotation;

        float _zoom;
    
        float _zoomDelta;
    
        MapPos _targetPos;
    
        bool _useDelta;
        bool _useTarget;
        bool _pinTarget;
    };
    
}

#endif

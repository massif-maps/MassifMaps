#ifndef _BASEMAPVIEW_I
#define _BASEMAPVIEW_I

%module BaseMapView

!proxy_imports(massif::BaseMapView, core.MapPos, core.MapVec, core.MapBounds, core.ScreenPos, core.ScreenBounds, components.Options, components.Layers, renderers.MapRenderer, renderers.RedrawRequestListener, ui.MapEventListener)

%{
#include "ui/BaseMapView.h"
#include "core/MapPos.h"
#include "core/ScreenPos.h"
%}

%include <massifswig.i>

%import "core/MapPos.i"
%import "core/MapBounds.i"
%import "core/ScreenPos.i"
%import "core/ScreenBounds.i"
%import "core/MapVec.i"
%import "components/Options.i"
%import "components/Layers.i"
%import "renderers/MapRenderer.i"
%import "renderers/RedrawRequestListener.i"
%import "ui/MapEventListener.i"

// THE CAMERA, on the facade (#159). Until these existed the typed sugar on both platforms called
// MapView directly, which is why it was the only part of the facade that could not be reproduced
// from the C ABI - and why NativeScript and React Native had no camera without a per-platform port.
//
// Read-only attributes: the camera is MOVED by the methods below, never by writing a property, so
// that one flight is one command instead of four racing animations (see BaseMapView::moveTo).
%attributeval(massif::BaseMapView, massif::MapPos, FocusPos, getFocusPos)
%attribute(massif::BaseMapView, float, Zoom, getZoom)
%attribute(massif::BaseMapView, float, Rotation, getRotation)
%attribute(massif::BaseMapView, float, Tilt, getTilt)
%attribute(massif::BaseMapView, bool, FlightActive, isFlightActive)
%attribute(massif::BaseMapView, float, FlightProgress, getFlightProgress)

!method(massif::BaseMapView, moveTo, arg(pos, pos), arg(zoom, float), arg(rotation, float), arg(tilt, float), returns(void))
!method(massif::BaseMapView, flyTo, arg(pos, pos), arg(zoom, float), arg(rotation, float), arg(tilt, float), arg(durationSeconds, float), returns(void))
!method(massif::BaseMapView, fitBounds, arg(bounds, json), arg(screenBounds, json), arg(integerZoom, bool), arg(durationSeconds, float), returns(void))
!method(massif::BaseMapView, screenToMap, arg(x, float), arg(y, float), returns(json))
!method(massif::BaseMapView, mapToScreen, arg(pos, pos), returns(json))
!method(massif::BaseMapView, stopFlight, returns(void))

%include "ui/BaseMapView.h"

#endif

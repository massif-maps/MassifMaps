#ifndef _CELESTIALCLICKINFO_I
#define _CELESTIALCLICKINFO_I

%module CelestialClickInfo

!proxy_imports(massif::CelestialClickInfo, celestial.CelestialObject, ui.ClickInfo)

%{
#include "ui/CelestialClickInfo.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "ui/ClickInfo.i"
%import "celestial/CelestialObject.i"

!shared_ptr(massif::CelestialClickInfo, ui.CelestialClickInfo)

%attribute(massif::CelestialClickInfo, massif::ClickType::ClickType, ClickType, getClickType)
%attributeval(massif::CelestialClickInfo, massif::ClickInfo, ClickInfo, getClickInfo)
!attributestring_polymorphic(massif::CelestialClickInfo, celestial.CelestialObject, CelestialObject, getCelestialObject)
%attribute(massif::CelestialClickInfo, float, Azimuth, getAzimuth)
%attribute(massif::CelestialClickInfo, float, Altitude, getAltitude)
%ignore massif::CelestialClickInfo::CelestialClickInfo;
!standard_equals(massif::CelestialClickInfo);

%include "ui/CelestialClickInfo.h"

#endif

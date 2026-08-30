#ifndef _LIGHTSTOP_I
#define _LIGHTSTOP_I

%module LightStop

!proxy_imports(massif::LightStop, graphics.Color)

%{
#include "components/LightStop.h"
%}

%include <std_string.i>
%include <std_vector.i>
%include <massifswig.i>

%import "graphics/Color.i"

!value_type(massif::LightStop, components.LightStop)

%attribute(massif::LightStop, float, SunAltitude, getSunAltitude)
%attributeval(massif::LightStop, massif::Color, AmbientColor, getAmbientColor)
%attribute(massif::LightStop, float, AmbientIntensity, getAmbientIntensity)
%attributeval(massif::LightStop, massif::Color, SunColor, getSunColor)
%attribute(massif::LightStop, float, SunIntensity, getSunIntensity)
!custom_equals(massif::LightStop);
!custom_tostring(massif::LightStop);

%include "components/LightStop.h"

!value_template(std::vector<massif::LightStop>, components.LightStopVector)

#endif

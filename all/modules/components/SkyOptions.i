#ifndef _SKYOPTIONS_I
#define _SKYOPTIONS_I

%module SkyOptions

!proxy_imports(massif::SkyOptions, graphics.Color)

%{
#include "components/SkyOptions.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "graphics/Color.i"

!shared_ptr(massif::SkyOptions, components.SkyOptions)

// The sky dome behind the map. Default-constructed, then every value is an ordinary property.
!spec(massif::SkyOptions, options, sky)

%attribute(massif::SkyOptions, bool, Enabled, isEnabled, setEnabled)
%attributeval(massif::SkyOptions, massif::Color, SkyColor, getSkyColor, setSkyColor)
%attributeval(massif::SkyOptions, massif::Color, HorizonColor, getHorizonColor, setHorizonColor)
%attributeval(massif::SkyOptions, massif::Color, GroundColor, getGroundColor, setGroundColor)
%attribute(massif::SkyOptions, float, HorizonBlend, getHorizonBlend, setHorizonBlend)
%attribute(massif::SkyOptions, bool, SunDiscEnabled, isSunDiscEnabled, setSunDiscEnabled)
%attributestring(massif::SkyOptions, std::string, ShaderSource, getShaderSource, setShaderSource)

%ignore massif::SkyOptions::OnChangeListener;
%ignore massif::SkyOptions::registerOnChangeListener;
%ignore massif::SkyOptions::unregisterOnChangeListener;

%include "components/SkyOptions.h"

#endif

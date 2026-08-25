#ifndef _FOGOPTIONS_I
#define _FOGOPTIONS_I

%module FogOptions

!proxy_imports(massif::FogOptions, graphics.Color)

%{
#include "components/FogOptions.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "graphics/Color.i"

!shared_ptr(massif::FogOptions, components.FogOptions)

// Fog, on the mapbox model. Independent of the terrain - it fogs a plain 2D map too. Default-constructed, then every value is an ordinary property.
!spec(massif::FogOptions, options, fog)

%attribute(massif::FogOptions, bool, Enabled, isEnabled, setEnabled)
%attributeval(massif::FogOptions, massif::Color, Color, getColor, setColor)
%attribute(massif::FogOptions, float, RangeStart, getRangeStart, setRangeStart)
%attribute(massif::FogOptions, float, RangeEnd, getRangeEnd, setRangeEnd)
%attributeval(massif::FogOptions, massif::Color, HighColor, getHighColor, setHighColor)
%attributeval(massif::FogOptions, massif::Color, SpaceColor, getSpaceColor, setSpaceColor)
%attribute(massif::FogOptions, float, HorizonBlend, getHorizonBlend, setHorizonBlend)
%attribute(massif::FogOptions, float, VerticalRangeStart, getVerticalRangeStart, setVerticalRangeStart)
%attribute(massif::FogOptions, float, VerticalRangeEnd, getVerticalRangeEnd, setVerticalRangeEnd)
%attribute(massif::FogOptions, float, StarIntensity, getStarIntensity, setStarIntensity)
%attributestring(massif::FogOptions, std::string, ShaderSource, getShaderSource, setShaderSource)

%ignore massif::FogOptions::OnChangeListener;
%ignore massif::FogOptions::registerOnChangeListener;
%ignore massif::FogOptions::unregisterOnChangeListener;

%include "components/FogOptions.h"

#endif

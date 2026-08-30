#ifndef _LIGHTOPTIONS_I
#define _LIGHTOPTIONS_I

%module LightOptions

!proxy_imports(massif::LightOptions, graphics.Color, components.LightStop)

%{
#include "components/LightOptions.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "graphics/Color.i"
%import "components/LightStop.i"

!shared_ptr(massif::LightOptions, components.LightOptions)

// Sun direction and colour, which the terrain and 3D buildings shade from. Default-constructed, then every value is an ordinary property.
!spec(massif::LightOptions, options, light)

%attribute(massif::LightOptions, float, SunAzimuth, getSunAzimuth, setSunAzimuth)
%attribute(massif::LightOptions, float, SunAltitude, getSunAltitude, setSunAltitude)
%attributeval(massif::LightOptions, massif::Color, SunColor, getSunColor, setSunColor)
%attribute(massif::LightOptions, float, SunIntensity, getSunIntensity, setSunIntensity)
%attribute(massif::LightOptions, float, AmbientIntensity, getAmbientIntensity, setAmbientIntensity)
%attributeval(massif::LightOptions, massif::Color, AmbientColor, getAmbientColor, setAmbientColor)
%attribute(massif::LightOptions, bool, SunOverridingStyle, isSunOverridingStyle, setSunOverridingStyle)
%attribute(massif::LightOptions, bool, DayCycleLightsEnabled, isDayCycleLightsEnabled, setDayCycleLightsEnabled)
%attributeval(massif::LightOptions, %arg(std::vector<massif::LightStop>), DayCycleLightStops, getDayCycleLightStops, setDayCycleLightStops)
%attributeval(massif::LightOptions, %arg(std::vector<massif::LightStop>), DayCycleRisingLightStops, getDayCycleRisingLightStops, setDayCycleRisingLightStops)
%attribute(massif::LightOptions, bool, TerrainLightingEnabled, isTerrainLightingEnabled, setTerrainLightingEnabled)
%attribute(massif::LightOptions, float, ShadowStrength, getShadowStrength, setShadowStrength)
%attribute(massif::LightOptions, int, ShadowMapSize, getShadowMapSize, setShadowMapSize)
%attribute(massif::LightOptions, int, ShadowCascades, getShadowCascades, setShadowCascades)
%attribute(massif::LightOptions, float, ShadowBias, getShadowBias, setShadowBias)
%attribute(massif::LightOptions, float, ShadowNormalOffset, getShadowNormalOffset, setShadowNormalOffset)
%attribute(massif::LightOptions, float, ShadowSoftness, getShadowSoftness, setShadowSoftness)
%attribute(massif::LightOptions, float, ShadowDistance, getShadowDistance, setShadowDistance)
%attribute(massif::LightOptions, int, ShadowCasterMargin, getShadowCasterMargin, setShadowCasterMargin)

%ignore massif::LightOptions::OnChangeListener;
%ignore massif::LightOptions::registerOnChangeListener;
%ignore massif::LightOptions::unregisterOnChangeListener;
%ignore massif::LightOptions::getSunDirection;

%include "components/LightOptions.h"

#endif

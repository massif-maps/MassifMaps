#ifndef _SUNEXPOSUREOPTIONS_I
#define _SUNEXPOSUREOPTIONS_I

%module SunExposureOptions

%{
#include "components/SunExposureOptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

!shared_ptr(massif::SunExposureOptions, components.SunExposureOptions)

%attribute(massif::SunExposureOptions, double, EyeHeight, getEyeHeight, setEyeHeight)
%attribute(massif::SunExposureOptions, double, MaxDistance, getMaxDistance, setMaxDistance)
%attribute(massif::SunExposureOptions, double, DiscRadius, getDiscRadius, setDiscRadius)
%attribute(massif::SunExposureOptions, double, DarkBelow, getDarkBelow, setDarkBelow)
%attribute(massif::SunExposureOptions, double, LowSunBelow, getLowSunBelow, setLowSunBelow)
%attribute(massif::SunExposureOptions, double, FirstStep, getFirstStep, setFirstStep)
%attribute(massif::SunExposureOptions, double, StepGrowth, getStepGrowth, setStepGrowth)
%attribute(massif::SunExposureOptions, double, LodRatio, getLodRatio, setLodRatio)
%attribute(massif::SunExposureOptions, double, MinTexel, getMinTexel, setMinTexel)
%attribute(massif::SunExposureOptions, double, Refraction, getRefraction, setRefraction)
%attribute(massif::SunExposureOptions, bool, LoadMissingTiles, isLoadMissingTiles, setLoadMissingTiles)
%attribute(massif::SunExposureOptions, int, LoadTimeoutMs, getLoadTimeoutMs, setLoadTimeoutMs)
%attribute(massif::SunExposureOptions, int, CacheMB, getCacheMB, setCacheMB)
%attribute(massif::SunExposureOptions, bool, ReleaseCache, isReleaseCache, setReleaseCache)

%include "components/SunExposureOptions.h"

#endif

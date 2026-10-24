#ifndef _POSTPROCESSEFFECT_I
#define _POSTPROCESSEFFECT_I

%module PostProcessEffect

!proxy_imports(massif::PostProcessEffect, graphics.Color)

%{
#include "renderers/PostProcessEffect.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "graphics/Color.i"

!shared_ptr(massif::PostProcessEffect, renderers.PostProcessEffect)

!spec(massif::PostProcessEffect, effect, postprocess)

%attributestring(massif::PostProcessEffect, std::string, Name, getName)
%attributestring(massif::PostProcessEffect, std::string, FragmentShader, getFragmentShader)
%attribute(massif::PostProcessEffect, bool, TerrainDepthRequired, isTerrainDepthRequired, setTerrainDepthRequired)
%attribute(massif::PostProcessEffect, bool, TerrainNormalsRequired, isTerrainNormalsRequired, setTerrainNormalsRequired)
%std_exceptions(massif::PostProcessEffect::PostProcessEffect)

%ignore massif::PostProcessEffect::getFloatParameters;
%ignore massif::PostProcessEffect::getColorParameters;

// Shader uniforms of an app-supplied effect, by name: set("floatParams.uIntensity", 1), set("colorParams.uTint", "#0af").
!indexed(massif::PostProcessEffect, floatParams, getFloatParameter, setFloatParameter, values(number))
!indexed(massif::PostProcessEffect, colorParams, getColorParameter, setColorParameter, values(color))

!standard_equals(massif::PostProcessEffect);

%include "renderers/PostProcessEffect.h"

#endif

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

%attributestring(massif::PostProcessEffect, std::string, Name, getName)
%attributestring(massif::PostProcessEffect, std::string, FragmentShader, getFragmentShader)
%attribute(massif::PostProcessEffect, bool, TerrainDepthRequired, isTerrainDepthRequired, setTerrainDepthRequired)
%attribute(massif::PostProcessEffect, bool, TerrainNormalsRequired, isTerrainNormalsRequired, setTerrainNormalsRequired)
%std_exceptions(massif::PostProcessEffect::PostProcessEffect)

%ignore massif::PostProcessEffect::getFloatParameters;
%ignore massif::PostProcessEffect::getColorParameters;

// Every uniform of an app-supplied effect goes through here, so without it the C ABI facade can
// create an effect and never tune it - which is why the web bindings could not drive the peak
// finder's ink at all while the SWIG bindings (which expose the method directly) could.
// Colours are not in the facade's method arg types; an app that needs them can inline the palette
// into the shader source it is already generating.
!method(massif::PostProcessEffect, setFloatParameter, arg(name, string), arg(value, float), returns(void))

!standard_equals(massif::PostProcessEffect);

%include "renderers/PostProcessEffect.h"

#endif

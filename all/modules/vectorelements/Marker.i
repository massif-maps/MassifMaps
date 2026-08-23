#ifndef _MARKER_I
#define _MARKER_I

%module Marker

!proxy_imports(massif::Marker, core.MapPos, graphics.Bitmap, geometry.Geometry, styles.MarkerStyle, vectorelements.Billboard)

%{
#include "vectorelements/Marker.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/MarkerStyle.i"
%import "vectorelements/Billboard.i"

!polymorphic_shared_ptr(massif::Marker, vectorelements.Marker)


!spec(massif::Marker, element, marker, alias(position, pos))
%attributestring(massif::Marker, std::shared_ptr<massif::MarkerStyle>, Style, getStyle, setStyle)
%std_exceptions(massif::Marker::Marker)
%std_exceptions(massif::Marker::setStyle)

%include "vectorelements/Marker.h"

#endif

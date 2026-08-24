#ifndef _LINE_I
#define _LINE_I

%module Line

!proxy_imports(massif::Line, core.MapPosVector, geometry.LineGeometry, styles.LineStyle, vectorelements.VectorElement)

%{
#include "vectorelements/Line.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "geometry/LineGeometry.i"
%import "styles/LineStyle.i"
%import "vectorelements/VectorElement.i"

!polymorphic_shared_ptr(massif::Line, vectorelements.Line)

!spec(massif::Line, element, line)
%csmethodmodifiers massif::Line::Geometry "public new";
!attributestring_polymorphic(massif::Line, geometry.LineGeometry, Geometry, getGeometry, setGeometry)
%attributestring(massif::Line, std::shared_ptr<massif::LineStyle>, Style, getStyle, setStyle)
%std_exceptions(massif::Line::Line)
%std_exceptions(massif::Line::setGeometry)
%std_exceptions(massif::Line::setStyle)
%ignore massif::Line::getDrawData;
%ignore massif::Line::setDrawData;

%include "vectorelements/Line.h"

#endif

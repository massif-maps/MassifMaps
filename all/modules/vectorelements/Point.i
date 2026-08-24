#ifndef _POINT_I
#define _POINT_I

%module Point

!proxy_imports(massif::Point, core.MapPos, geometry.PointGeometry, styles.PointStyle, vectorelements.VectorElement)

%{
#include "vectorelements/Point.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "geometry/PointGeometry.i"
%import "styles/PointStyle.i"
%import "vectorelements/VectorElement.i"

!polymorphic_shared_ptr(massif::Point, vectorelements.Point)

!spec(massif::Point, element, point, alias(position, pos))
%csmethodmodifiers massif::Point::Geometry "public new";
!attributestring_polymorphic(massif::Point, geometry.PointGeometry, Geometry, getGeometry, setGeometry)
%attributestring(massif::Point, std::shared_ptr<massif::PointStyle>, Style, getStyle, setStyle)
%std_exceptions(massif::Point::Point)
%std_exceptions(massif::Point::setGeometry)
%std_exceptions(massif::Point::setStyle)
%ignore massif::Point::getDrawData;
%ignore massif::Point::setDrawData;

%include "vectorelements/Point.h"

#endif

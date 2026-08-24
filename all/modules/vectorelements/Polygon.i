#ifndef _POLYGON_I
#define _POLYGON_I

%module Polygon

!proxy_imports(massif::Polygon, core.MapPosVector, core.MapPosVectorVector, geometry.PolygonGeometry, styles.PolygonStyle, vectorelements.VectorElement)

%{
#include "vectorelements/Polygon.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "geometry/PolygonGeometry.i"
%import "styles/PolygonStyle.i"
%import "vectorelements/VectorElement.i"

!polymorphic_shared_ptr(massif::Polygon, vectorelements.Polygon)

!spec(massif::Polygon, element, polygon)
%attributestring(massif::Polygon, std::shared_ptr<massif::PolygonStyle>, Style, getStyle, setStyle)
%csmethodmodifiers massif::Polygon::Geometry "public new";
!attributestring_polymorphic(massif::Polygon, geometry.PolygonGeometry, Geometry, getGeometry, setGeometry)
%std_exceptions(massif::Polygon::Polygon)
%std_exceptions(massif::Polygon::setGeometry)
%std_exceptions(massif::Polygon::setStyle)
%ignore massif::Polygon::getDrawData;
%ignore massif::Polygon::setDrawData;

%include "vectorelements/Polygon.h"

#endif

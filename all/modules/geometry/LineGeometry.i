#ifndef _LINEGEOMETRY_I
#define _LINEGEOMETRY_I

%module LineGeometry

!proxy_imports(massif::LineGeometry, core.MapPos, core.MapPosVector, geometry.Geometry)

%{
#include "geometry/LineGeometry.h"	
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "geometry/Geometry.i"

!polymorphic_shared_ptr(massif::LineGeometry, geometry.LineGeometry)
!value_type(std::vector<std::shared_ptr<massif::LineGeometry> >, geometry.LineGeometryVector)

!spec(massif::LineGeometry, geometry, line)
%attributeval(massif::LineGeometry, std::vector<massif::MapPos>, Poses, getPoses)

%include "geometry/LineGeometry.h"

!value_template(std::vector<std::shared_ptr<massif::LineGeometry> >, geometry.LineGeometryVector)

#endif

#ifndef _POINTGEOMETRY_I
#define _POINTGEOMETRY_I

%module PointGeometry

!proxy_imports(massif::PointGeometry, core.MapPos, geometry.Geometry)

%{
#include "geometry/PointGeometry.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "geometry/Geometry.i"

!polymorphic_shared_ptr(massif::PointGeometry, geometry.PointGeometry)

!spec(massif::PointGeometry, geometry, point)
!value_type(std::vector<std::shared_ptr<massif::PointGeometry> >, geometry.PointGeometryVector)

%attributeval(massif::PointGeometry, massif::MapPos, Pos, getPos)

%include "geometry/PointGeometry.h"

!value_template(std::vector<std::shared_ptr<massif::PointGeometry> >, geometry.PointGeometryVector)

#endif

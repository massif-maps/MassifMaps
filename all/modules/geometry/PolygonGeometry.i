#ifndef _POLYGONGEOMETRY_I
#define _POLYGONGEOMETRY_I

%module PolygonGeometry

!proxy_imports(massif::PolygonGeometry, core.MapPos, core.MapPosVector, core.MapPosVectorVector, geometry.Geometry)

%{
#include "geometry/PolygonGeometry.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "geometry/Geometry.i"

!polymorphic_shared_ptr(massif::PolygonGeometry, geometry.PolygonGeometry)
!value_type(std::vector<std::shared_ptr<massif::PolygonGeometry> >, geometry.PolygonGeometryVector)

// Three constructors: "poses" alone, "poses" + "holes", or "rings" (the outline first).
!spec(massif::PolygonGeometry, geometry, polygon)
%attributeval(massif::PolygonGeometry, std::vector<massif::MapPos>, Poses, getPoses)
%attributeval(massif::PolygonGeometry, std::vector<std::vector<massif::MapPos> >, Holes, getHoles)
%attributeval(massif::PolygonGeometry, std::vector<std::vector<massif::MapPos> >, Rings, getRings)

%include "geometry/PolygonGeometry.h"

!value_template(std::vector<std::shared_ptr<massif::PolygonGeometry> >, geometry.PolygonGeometryVector)

#endif

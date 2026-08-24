#ifndef _GEOMETRY_I
#define _GEOMETRY_I

%module Geometry

!proxy_imports(massif::Geometry, core.MapPos, core.MapBounds)

%{
#include "geometry/Geometry.h"
#include <memory>
#include <string>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "core/MapBounds.i"

!polymorphic_shared_ptr(massif::Geometry, geometry.Geometry)
!value_type(std::vector<std::shared_ptr<massif::Geometry> >, geometry.GeometryVector)

%attributeval(massif::Geometry, massif::MapBounds, Bounds, getBounds)
%attributeval(massif::Geometry, massif::MapPos, CenterPos, getCenterPos)
%attributestring(massif::Geometry, std::string, GeoJSON, getGeoJSON)
!standard_equals(massif::Geometry);

!enum(massif::GeometryType::GeometryType)
%attribute(massif::Geometry, massif::GeometryType::GeometryType, Type, getType)

%include "geometry/Geometry.h"

!value_template(std::vector<std::shared_ptr<massif::Geometry> >, geometry.GeometryVector)

#endif

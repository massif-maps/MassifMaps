#ifndef _PROJECTION_I
#define _PROJECTION_I

%module Projection

!proxy_imports(massif::Projection, core.MapBounds, core.MapPos)

%{
#include "projections/Projection.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapBounds.i"
%import "core/MapPos.i"

!polymorphic_shared_ptr(massif::Projection, projections.Projection)


!spec(massif::Projection, projection, -)
%attributeval(massif::Projection, massif::MapBounds, Bounds, getBounds)
%attributestring(massif::Projection, std::string, Name, getName)
!objc_rename(fromLat) massif::Projection::fromLatLong;
%ignore massif::Projection::fromInternal;
%ignore massif::Projection::toInternal;
!standard_equals(massif::Projection);

%include "projections/Projection.h"

#endif

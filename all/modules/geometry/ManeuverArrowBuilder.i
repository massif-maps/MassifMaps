#ifndef _MANEUVERARROWBUILDER_I
#define _MANEUVERARROWBUILDER_I

%module ManeuverArrowBuilder

!proxy_imports(massif::ManeuverArrowBuilder, core.MapPos, core.MapPosVector, geometry.FeatureCollection, projections.Projection)

%{
#include "geometry/ManeuverArrowBuilder.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "geometry/FeatureCollection.i"
%import "projections/Projection.i"

!shared_ptr(massif::ManeuverArrowBuilder, geometry.ManeuverArrowBuilder)
!spec(massif::ManeuverArrowBuilder, geometry, maneuver-arrow)
// The facade drops the projection: positions arrive in the call's projection, the arrow leaves as GeoJSON.
!method(massif::ManeuverArrowBuilder, buildArrow, arg(points, positions), arg(maneuverPos, pos), returns(json))
!method(massif::ManeuverArrowBuilder, buildArrowAtIndex, arg(points, positions), arg(maneuverIndex, int), returns(json))

%attribute(massif::ManeuverArrowBuilder, float, LengthBefore, getLengthBefore, setLengthBefore)
%attribute(massif::ManeuverArrowBuilder, float, LengthAfter, getLengthAfter, setLengthAfter)
%std_exceptions(massif::ManeuverArrowBuilder::buildArrow)
%std_exceptions(massif::ManeuverArrowBuilder::buildArrowAtIndex)

%include "geometry/ManeuverArrowBuilder.h"

#endif

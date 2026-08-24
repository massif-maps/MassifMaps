#ifndef _ROUTINGRESULT_I
#define _ROUTINGRESULT_I

#pragma SWIG nowarn=325

%module RoutingResult

#ifdef _MASSIF_ROUTING_SUPPORT

!proxy_imports(massif::RoutingResult, core.MapPos, core.MapPosVector, projections.Projection, routing.RoutingInstruction, routing.RoutingInstructionVector)

%{
#include "routing/RoutingResult.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "projections/Projection.i"
%import "routing/RoutingInstruction.i"

!shared_ptr(massif::RoutingResult, routing.RoutingResult)

!method(massif::RoutingResult, getInstruction, arg(index, int), returns(object, massif::RoutingInstruction))
// The whole polyline as a flat array, for the same reason getElevations is one.
!method(massif::RoutingResult, getPoints, returns(doubles))
%attributestring(massif::RoutingResult, std::shared_ptr<massif::Projection>, Projection, getProjection)
%attributeval(massif::RoutingResult, std::vector<massif::MapPos>, Points, getPoints)
%attributeval(massif::RoutingResult, std::vector<massif::RoutingInstruction>, Instructions, getInstructions)
%attribute(massif::RoutingResult, int, InstructionCount, getInstructionCount)
// Every maneuver in one read: getInstruction(i) is a call per instruction, times nine fields.
%attributestring(massif::RoutingResult, std::string, InstructionsJSON, getInstructionsJSON)
%attribute(massif::RoutingResult, int, PointCount, getPointCount)
%attribute(massif::RoutingResult, double, TotalDistance, getTotalDistance)
%attribute(massif::RoutingResult, double, TotalTime, getTotalTime)
%attributestring(massif::RoutingResult, std::string, RawResult, getRawResult)
%std_exceptions(massif::RoutingResult::RoutingResult)
!standard_equals(massif::RoutingResult);
!custom_tostring(massif::RoutingResult);

%include "routing/RoutingResult.h"

#endif

#endif

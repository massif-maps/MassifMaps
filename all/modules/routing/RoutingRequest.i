#ifndef _ROUTINGREQUEST_I
#define _ROUTINGREQUEST_I

#pragma SWIG nowarn=325

%module RoutingRequest

#ifdef _MASSIF_ROUTING_SUPPORT

!proxy_imports(massif::RoutingRequest, core.MapPos, core.MapPosVector, core.Variant, core.VariantVector, projections.Projection)

%{
#include "routing/RoutingRequest.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_map.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "core/Variant.i"
%import "projections/Projection.i"

!shared_ptr(massif::RoutingRequest, routing.RoutingRequest)

!spec(massif::RoutingRequest, routing, -)

!method(massif::RoutingRequest, setCustomParameter, arg(name, string), arg(value, json), returns(void))
%attributestring(massif::RoutingRequest, std::shared_ptr<massif::Projection>, Projection, getProjection)
%attributeval(massif::RoutingRequest, std::vector<massif::MapPos>, Points, getPoints)
%ignore massif::RoutingRequest::getPointParameters;
%ignore massif::RoutingRequest::getCustomParameters;
%std_exceptions(massif::RoutingRequest::RoutingRequest)
!standard_equals(massif::RoutingRequest);
!custom_tostring(massif::RoutingRequest);

%include "routing/RoutingRequest.h"

#endif

#endif

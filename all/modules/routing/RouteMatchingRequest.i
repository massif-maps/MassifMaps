#ifndef _ROUTEMATCHINGREQUEST_I
#define _ROUTEMATCHINGREQUEST_I

#pragma SWIG nowarn=325

%module RouteMatchingRequest

#ifdef _MASSIF_ROUTING_SUPPORT

!proxy_imports(massif::RouteMatchingRequest, core.MapPos, core.MapPosVector, core.Variant, projections.Projection)

%{
#include "routing/RouteMatchingRequest.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_map.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "core/Variant.i"
%import "projections/Projection.i"

!shared_ptr(massif::RouteMatchingRequest, routing.RouteMatchingRequest)

!spec(massif::RouteMatchingRequest, routing, -)

!method(massif::RouteMatchingRequest, setCustomParameter, arg(name, string), arg(value, json), returns(void))
%attributestring(massif::RouteMatchingRequest, std::shared_ptr<massif::Projection>, Projection, getProjection)
%attributeval(massif::RouteMatchingRequest, std::vector<massif::MapPos>, Points, getPoints)
%attribute(massif::RouteMatchingRequest, float, Accuracy, getAccuracy)
%ignore massif::RouteMatchingRequest::getPointParameters;
%ignore massif::RouteMatchingRequest::getCustomParameters;
%std_exceptions(massif::RouteMatchingRequest::RouteMatchingRequest)
!standard_equals(massif::RouteMatchingRequest);
!custom_tostring(massif::RouteMatchingRequest);

%include "routing/RouteMatchingRequest.h"

#endif

#endif

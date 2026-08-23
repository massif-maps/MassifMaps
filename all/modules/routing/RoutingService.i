#ifndef _ROUTINGSERVICE_I
#define _ROUTINGSERVICE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module(directors="1") RoutingService

#ifdef _MASSIF_ROUTING_SUPPORT

!proxy_imports(massif::RoutingService, routing.RoutingRequest, routing.RoutingResult, routing.RouteMatchingRequest, routing.RouteMatchingResult)

%{
#include "routing/RoutingService.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "routing/RoutingRequest.i"
%import "routing/RoutingResult.i"
%import "routing/RouteMatchingRequest.i"
%import "routing/RouteMatchingResult.i"

!polymorphic_shared_ptr(massif::RoutingService, routing.RoutingService)

!method(massif::RoutingService, calculateRoute, arg(request, handle), returns(object, massif::RoutingResult))
%attributestring(massif::RoutingService, std::string, Profile, getProfile, setProfile)
%std_exceptions(massif::RoutingService::setProfile)
%std_io_exceptions(massif::RoutingService::matchRoute)
%std_io_exceptions(massif::RoutingService::calculateRoute)

%feature("director") massif::RoutingService;

%include "routing/RoutingService.h"

#endif

#endif

#ifndef _VALHALLAONLINEROUTINGSERVICE_I
#define _VALHALLAONLINEROUTINGSERVICE_I

%module(directors="1") ValhallaOnlineRoutingService

#ifdef _MASSIF_ROUTING_SUPPORT

!proxy_imports(massif::ValhallaOnlineRoutingService, routing.RoutingService, routing.RoutingRequest, routing.RoutingResult, routing.RouteMatchingRequest, routing.RouteMatchingResult, core.StringMap)

%{
#include "routing/ValhallaOnlineRoutingService.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>
%include <std_map.i>

%import "routing/RoutingService.i"
%import "core/StringMap.i"

!polymorphic_shared_ptr(massif::ValhallaOnlineRoutingService, routing.ValhallaOnlineRoutingService)


!spec(massif::ValhallaOnlineRoutingService, routing, valhalla-online)
%attributestring(massif::ValhallaOnlineRoutingService, std::string, CustomServiceURL, getCustomServiceURL, setCustomServiceURL)

%attribute(massif::ValhallaOnlineRoutingService, int, Timeout, getTimeout, setTimeout)
%attributeval(massif::ValhallaOnlineRoutingService, %arg(std::map<std::string, std::string>), HTTPHeaders, getHTTPHeaders, setHTTPHeaders)
%std_io_exceptions(massif::ValhallaOnlineRoutingService::matchRoute)
%std_io_exceptions(massif::ValhallaOnlineRoutingService::calculateRoute)

%feature("director") massif::ValhallaOnlineRoutingService;

%include "routing/ValhallaOnlineRoutingService.h"

#endif

#endif

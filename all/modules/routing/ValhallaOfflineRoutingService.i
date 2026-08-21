#ifndef _VALHALLAOFFLINEROUTINGSERVICE_I
#define _VALHALLAOFFLINEROUTINGSERVICE_I

%module(directors="1") ValhallaOfflineRoutingService

#if defined(_MASSIF_ROUTING_SUPPORT) && defined(_MASSIF_VALHALLA_ROUTING_SUPPORT) && defined(_MASSIF_OFFLINE_SUPPORT)

!proxy_imports(massif::ValhallaOfflineRoutingService, core.Variant, routing.RoutingService, routing.RoutingRequest, routing.RoutingResult, routing.RouteMatchingRequest, routing.RouteMatchingResult, datasources.TileDataSource, rastertiles.ElevationDecoder)

%{
#include "routing/ValhallaOfflineRoutingService.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/Variant.i"
%import "routing/RoutingService.i"
%import "datasources/TileDataSource.i"
%import "rastertiles/ElevationDecoder.i"

!polymorphic_shared_ptr(massif::ValhallaOfflineRoutingService, routing.ValhallaOfflineRoutingService)


!spec(massif::ValhallaOfflineRoutingService, routing, valhalla-offline)
%std_io_exceptions(massif::ValhallaOfflineRoutingService::ValhallaOfflineRoutingService)
%std_io_exceptions(massif::ValhallaOfflineRoutingService::matchRoute)
%std_io_exceptions(massif::ValhallaOfflineRoutingService::calculateRoute)

%feature("director") massif::ValhallaOfflineRoutingService;

%include "routing/ValhallaOfflineRoutingService.h"

#endif

#endif

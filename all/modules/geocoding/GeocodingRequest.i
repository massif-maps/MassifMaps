#ifndef _GEOCODINGREQUEST_I
#define _GEOCODINGREQUEST_I

#pragma SWIG nowarn=325

%module GeocodingRequest

#ifdef _MASSIF_GEOCODING_SUPPORT

!proxy_imports(massif::GeocodingRequest, core.MapPos, core.Variant, projections.Projection)

%{
#include "geocoding/GeocodingRequest.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "core/Variant.i"
%import "projections/Projection.i"

!shared_ptr(massif::GeocodingRequest, geocoding.GeocodingRequest)

!spec(massif::GeocodingRequest, geocoding, -)

%attributestring(massif::GeocodingRequest, std::string, Query, getQuery)
%attributestring(massif::GeocodingRequest, std::shared_ptr<massif::Projection>, Projection, getProjection)
%attributeval(massif::GeocodingRequest, massif::MapPos, Location, getLocation, setLocation)
%attribute(massif::GeocodingRequest, float, LocationRadius, getLocationRadius, setLocationRadius)
%ignore massif::GeocodingRequest::isLocationDefined;
%ignore massif::GeocodingRequest::getCustomParameters;
%std_exceptions(massif::GeocodingRequest::GeocodingRequest)
!standard_equals(massif::GeocodingRequest);
!custom_tostring(massif::GeocodingRequest);

%include "geocoding/GeocodingRequest.h"

#endif

#endif

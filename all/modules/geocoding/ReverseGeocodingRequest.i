#ifndef _REVERSEGEOCODINGREQUEST_I
#define _REVERSEGEOCODINGREQUEST_I

#pragma SWIG nowarn=325

%module ReverseGeocodingRequest

#ifdef _MASSIF_GEOCODING_SUPPORT

!proxy_imports(massif::ReverseGeocodingRequest, core.MapPos, core.Variant, projections.Projection)

%{
#include "geocoding/ReverseGeocodingRequest.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "core/Variant.i"
%import "projections/Projection.i"

!shared_ptr(massif::ReverseGeocodingRequest, geocoding.ReverseGeocodingRequest)

!spec(massif::ReverseGeocodingRequest, geocoding, -)

%attributeval(massif::ReverseGeocodingRequest, massif::MapPos, Location, getLocation)
%attribute(massif::ReverseGeocodingRequest, float, SearchRadius, getSearchRadius, setSearchRadius)
%attributestring(massif::ReverseGeocodingRequest, std::shared_ptr<massif::Projection>, Projection, getProjection)
%ignore massif::ReverseGeocodingRequest::getCustomParameters;
%std_exceptions(massif::ReverseGeocodingRequest::ReverseGeocodingRequest)
!standard_equals(massif::ReverseGeocodingRequest);
!custom_tostring(massif::ReverseGeocodingRequest);

%include "geocoding/ReverseGeocodingRequest.h"

#endif

#endif

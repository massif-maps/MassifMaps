#ifndef _GEOCODINGSERVICE_I
#define _GEOCODINGSERVICE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module(directors="1") GeocodingService

#ifdef _MASSIF_GEOCODING_SUPPORT

!proxy_imports(massif::GeocodingService, geocoding.GeocodingRequest, geocoding.GeocodingResult)

%{
#include "geocoding/GeocodingService.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "geocoding/GeocodingRequest.i"
%import "geocoding/GeocodingResult.i"

!polymorphic_shared_ptr(massif::GeocodingService, geocoding.GeocodingService)

// One string for the whole answer: a FeatureCollection whose features carry the result's
// "address" and "rank". A binding walking the results paid a crossing per feature to rebuild it.
!method(massif::GeocodingService, calculateAddresses, arg(request, handle), returns(json))

%attribute(massif::GeocodingService, bool, Autocomplete, isAutocomplete, setAutocomplete)
%attributestring(massif::GeocodingService, std::string, Language, getLanguage, setLanguage)
%attribute(massif::GeocodingService, int, MaxResults, getMaxResults, setMaxResults)
%std_exceptions(massif::GeocodingService::setAutocomplete)
%std_exceptions(massif::GeocodingService::setLanguage)
%std_exceptions(massif::GeocodingService::setNumResults)
%std_io_exceptions(massif::GeocodingService::calculateAddresses)

%feature("director") massif::GeocodingService;

%include "geocoding/GeocodingService.h"

#endif

#endif

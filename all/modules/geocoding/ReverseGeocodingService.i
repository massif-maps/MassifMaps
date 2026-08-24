#ifndef _REVERSEGEOCODINGSERVICE_I
#define _REVERSEGEOCODINGSERVICE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module(directors="1") ReverseGeocodingService

#ifdef _MASSIF_GEOCODING_SUPPORT

!proxy_imports(massif::ReverseGeocodingService, geocoding.ReverseGeocodingRequest, geocoding.GeocodingResult)

%{
#include "geocoding/ReverseGeocodingService.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "geocoding/ReverseGeocodingRequest.i"
%import "geocoding/GeocodingResult.i"

!polymorphic_shared_ptr(massif::ReverseGeocodingService, geocoding.ReverseGeocodingService)

!method(massif::ReverseGeocodingService, calculateAddresses, arg(request, handle), returns(json))

%attributestring(massif::ReverseGeocodingService, std::string, Language, getLanguage, setLanguage)
%std_exceptions(massif::ReverseGeocodingService::setLanguage)
%std_io_exceptions(massif::ReverseGeocodingService::calculateAddresses)

%feature("director") massif::ReverseGeocodingService;

%include "geocoding/ReverseGeocodingService.h"

#endif

#endif

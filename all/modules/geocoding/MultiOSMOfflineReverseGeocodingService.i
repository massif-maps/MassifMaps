#ifndef _OSMOFFLINEREVERSEGEOCODINGSERVICE_I
#define _OSMOFFLINEREVERSEGEOCODINGSERVICE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module(directors="1") MultiOSMOfflineReverseGeocodingService

#if defined(_MASSIF_GEOCODING_SUPPORT) && defined(_MASSIF_OFFLINE_SUPPORT)

!proxy_imports(massif::MultiOSMOfflineReverseGeocodingService, geocoding.ReverseGeocodingService, geocoding.ReverseGeocodingRequest, geocoding.GeocodingResult, projections.Projection)

%{
#include "geocoding/MultiOSMOfflineReverseGeocodingService.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "geocoding/ReverseGeocodingService.i"
%import "geocoding/ReverseGeocodingRequest.i"
%import "geocoding/GeocodingResult.i"

!polymorphic_shared_ptr(massif::MultiOSMOfflineReverseGeocodingService, geocoding.MultiOSMOfflineReverseGeocodingService)

!spec(massif::MultiOSMOfflineReverseGeocodingService, geocoding, multi-osm-offline-reverse)
!method(massif::MultiOSMOfflineReverseGeocodingService, add, arg(database, string), returns(void))
!method(massif::MultiOSMOfflineReverseGeocodingService, remove, arg(database, string), returns(bool))

%std_io_exceptions(massif::MultiOSMOfflineReverseGeocodingService::MultiOSMOfflineReverseGeocodingService)
%std_io_exceptions(massif::MultiOSMOfflineReverseGeocodingService::calculateAddresses)

%feature("director") massif::MultiOSMOfflineReverseGeocodingService;

%include "geocoding/MultiOSMOfflineReverseGeocodingService.h"

#endif

#endif
